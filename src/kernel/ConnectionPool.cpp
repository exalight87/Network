#define NOMINMAX
#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <test_curl/kernel/ConnectionPool.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
constexpr WORD WakeWinsockVersion = MAKEWORD(2, 2);
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
constexpr auto IdleReadPollQuantum = std::chrono::milliseconds(5);
constexpr auto EmptyIdleWaitQuantum = std::chrono::milliseconds(100);
void _CloseSocket(SOCKET socket);
bool _SetNonBlocking(SOCKET socket);
unsigned int _WorkerThreadCount();
}

ConnectionPool::ConnectionPool(std::function<Action(std::stop_token, std::shared_ptr<SocketConnection>)> callable)
    : m_entryPoint(callable)
{
    const unsigned int numThreads = _WorkerThreadCount();

    for (unsigned int i = 0; i < numThreads; ++i)
    {
        m_workers.emplace_back([this](std::stop_token stopToken) { m_worker(stopToken); });
    }

    m_cleaner = std::jthread(
        [this](std::stop_token stopToken)
        {
            auto removeClosedConnections = [this]()
            {
                std::vector<std::size_t> toRemove;

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    std::erase_if(m_idleConnections, [](const auto& connection)
                                  { return connection == nullptr || connection->isClosed(); });
                    for (std::size_t idx = 0; idx < m_connections.size(); ++idx)
                    {
                        if (m_connections[idx]->isClosed())
                        {
                            toRemove.push_back(idx);
                        }
                    }
                }

                for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
                {
                    std::size_t idx = *it;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (idx < m_connections.size())
                    {
                        std::swap(m_connections[idx], m_connections.back());
                        m_connections.pop_back();
                    }
                }
            };

            while (!stopToken.stop_requested())
            {
                removeClosedConnections();
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(1s);
            }
        });

    m_poller = std::jthread([this](std::stop_token stopToken) { m_pollIdleConnections(stopToken); });
}

ConnectionPool::~ConnectionPool()
{
    stop();
}

void ConnectionPool::m_worker(std::stop_token stopToken)
{
    while (!stopToken.stop_requested() && !m_stop)
    {
        std::shared_ptr<SocketConnection> connection;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_workCondition.wait(lock, [this, &stopToken]()
                                 { return !m_workQueue.empty() || stopToken.stop_requested() || m_stop; });

            if (stopToken.stop_requested() || m_stop)
                break;

            if (!m_workQueue.empty())
            {
                connection = m_workQueue.front();
                m_workQueue.pop();
            }
        }

        if (connection && !connection->isClosed())
        {
            const Action action = m_entryPoint(stopToken, connection);

            if (stopToken.stop_requested() || m_stop)
            {
                break;
            }

            switch (action)
            {
            case Action::WaitForRead:
                m_waitForRead(std::move(connection));
                break;
            case Action::Requeue:
                m_push(std::move(connection));
                break;
            case Action::Close:
                if (connection && !connection->isClosed())
                {
                    connection->requestClose();
                    connection->disconnect();
                }
                break;
            }
        }
    }
}

void ConnectionPool::m_push(SocketConnection&& connection)
{
    auto connPtr = std::make_shared<SocketConnection>(std::move(connection));
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connections.push_back(connPtr);
        m_workQueue.push(connPtr);
    }
    m_workCondition.notify_one();
}

void ConnectionPool::m_push(std::shared_ptr<SocketConnection> connection)
{
    if (!connection || connection->isClosed())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_workQueue.push(std::move(connection));
    }
    m_workCondition.notify_one();
}

void ConnectionPool::m_waitForRead(std::shared_ptr<SocketConnection> connection)
{
    if (!connection || connection->isClosed() || connection->closeRequested())
    {
        return;
    }

    connection->markIdle();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_idleConnections.push_back(std::move(connection));
    }
    m_signalIdlePoller();
    m_idleCondition.notify_one();
}

void ConnectionPool::m_pollIdleConnections(std::stop_token stopToken)
{
    using namespace std::chrono_literals;

    while (!stopToken.stop_requested() && !m_stop)
    {
        std::vector<std::shared_ptr<SocketConnection>> idleConnections;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_idleConnections.empty())
            {
                m_idleCondition.wait_for(
                    lock, EmptyIdleWaitQuantum, [this, &stopToken]()
                    { return !m_idleConnections.empty() || stopToken.stop_requested() || m_stop; });
            }

            if (stopToken.stop_requested() || m_stop)
            {
                break;
            }

            idleConnections = m_idleConnections;
        }

        m_initializeIdlePollWakeSignal();

        if (idleConnections.empty())
        {
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        std::vector<std::shared_ptr<SocketConnection>> toClose;
        std::vector<std::shared_ptr<SocketConnection>> ready;

        fd_set readSet;
        FD_ZERO(&readSet);

        SOCKET maxHandle = 0;
        bool hasSelectableSocket = false;

        if (m_idlePollWakeRead != INVALID_SOCKET)
        {
            FD_SET(m_idlePollWakeRead, &readSet);
            maxHandle = std::max(maxHandle, m_idlePollWakeRead);
            hasSelectableSocket = true;
        }

        for (const auto& connection : idleConnections)
        {
            if (!connection || connection->isClosed() || connection->closeRequested() ||
                connection->requestTimedOut(now) || connection->keepAliveTimedOut(now))
            {
                toClose.push_back(connection);
                continue;
            }

#ifndef _WIN32
            if (connection->handle() >= FD_SETSIZE)
            {
                ready.push_back(connection);
                continue;
            }
#endif
            FD_SET(connection->handle(), &readSet);
            maxHandle = std::max(maxHandle, connection->handle());
            hasSelectableSocket = true;
        }

        if (hasSelectableSocket)
        {
            timeval timeout{};
            timeout.tv_sec = static_cast<long>(IdleReadPollQuantum.count() / 1000);
            timeout.tv_usec = static_cast<long>((IdleReadPollQuantum.count() % 1000) * 1000);

#ifdef _WIN32
            const int selected = select(0, &readSet, nullptr, nullptr, &timeout);
#else
            const int selected = select(maxHandle + 1, &readSet, nullptr, nullptr, &timeout);
#endif
            if (selected > 0)
            {
                if (m_idlePollWakeRead != INVALID_SOCKET && FD_ISSET(m_idlePollWakeRead, &readSet))
                {
                    m_drainIdlePollerSignal();
                }

                for (const auto& connection : idleConnections)
                {
                    if (connection && !connection->isClosed() && FD_ISSET(connection->handle(), &readSet))
                    {
                        ready.push_back(connection);
                    }
                }
            }
        }
        else
        {
            std::this_thread::sleep_for(10ms);
        }

        if (toClose.empty() && ready.empty())
        {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto removeFromIdle = [this](const std::shared_ptr<SocketConnection>& connection)
            { std::erase(m_idleConnections, connection); };

            for (const auto& connection : toClose)
            {
                removeFromIdle(connection);
            }

            for (const auto& connection : ready)
            {
                removeFromIdle(connection);
                if (connection && !connection->isClosed())
                {
                    m_workQueue.push(connection);
                }
            }
        }

        for (const auto& connection : toClose)
        {
            if (connection && !connection->isClosed())
            {
                connection->requestClose();
                connection->disconnect();
            }
        }

        if (!ready.empty())
        {
            m_workCondition.notify_all();
        }
    }
}

void ConnectionPool::stop()
{
    if (m_stop.exchange(true))
    {
        return;
    }

    for (auto& worker : m_workers)
    {
        worker.request_stop();
    }

    if (m_cleaner.joinable())
    {
        m_cleaner.request_stop();
    }

    if (m_poller.joinable())
    {
        m_poller.request_stop();
    }

    m_workCondition.notify_all();
    m_signalIdlePoller();
    m_idleCondition.notify_all();

    std::vector<std::shared_ptr<SocketConnection>> connections;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        connections = m_connections;
    }

    for (auto& connection : connections)
    {
        if (connection)
        {
            connection->requestClose();
            connection->disconnect();
        }
    }

    m_workers.clear();

    if (m_cleaner.joinable())
    {
        m_cleaner.join();
    }

    if (m_poller.joinable())
    {
        m_poller.join();
    }

    m_closeIdlePollWakeSignal();

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        while (!m_workQueue.empty())
        {
            m_workQueue.pop();
        }

        m_idleConnections.clear();
        m_connections.clear();
    }
}

bool ConnectionPool::m_initializeIdlePollWakeSignal()
{
    if (m_idlePollWakeRead != INVALID_SOCKET && m_idlePollWakeWrite != INVALID_SOCKET)
    {
        return true;
    }

#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(WakeWinsockVersion, &data) != 0)
    {
        return false;
    }
#endif

    const SOCKET readSocket = socket(AF_INET, SOCK_DGRAM, 0);
    const SOCKET writeSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (readSocket == INVALID_SOCKET || writeSocket == INVALID_SOCKET)
    {
        _CloseSocket(readSocket);
        _CloseSocket(writeSocket);
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(readSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        _CloseSocket(readSocket);
        _CloseSocket(writeSocket);
        return false;
    }

    socklen_t addressSize = sizeof(address);
    if (getsockname(readSocket, reinterpret_cast<sockaddr*>(&address), &addressSize) != 0 ||
        connect(writeSocket, reinterpret_cast<sockaddr*>(&address), addressSize) != 0)
    {
        _CloseSocket(readSocket);
        _CloseSocket(writeSocket);
        return false;
    }

    _SetNonBlocking(readSocket);
    _SetNonBlocking(writeSocket);

    m_idlePollWakeRead = readSocket;
    m_idlePollWakeWrite = writeSocket;
    return true;
}

void ConnectionPool::m_signalIdlePoller()
{
    if (m_idlePollWakeWrite == INVALID_SOCKET)
    {
        return;
    }

    const char byte = '\0';
#ifdef _WIN32
    send(m_idlePollWakeWrite, &byte, 1, 0);
#else
    send(m_idlePollWakeWrite, &byte, 1, MSG_NOSIGNAL);
#endif
}

void ConnectionPool::m_drainIdlePollerSignal()
{
    if (m_idlePollWakeRead == INVALID_SOCKET)
    {
        return;
    }

    std::array<char, 64> buffer{};
    while (true)
    {
        const int received = recv(m_idlePollWakeRead, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received > 0)
        {
            continue;
        }

#ifdef _WIN32
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK)
        {
            break;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }
#endif
        break;
    }
}

void ConnectionPool::m_closeIdlePollWakeSignal()
{
    _CloseSocket(m_idlePollWakeRead);
    _CloseSocket(m_idlePollWakeWrite);
    m_idlePollWakeRead = INVALID_SOCKET;
    m_idlePollWakeWrite = INVALID_SOCKET;
}

namespace
{
void _CloseSocket(SOCKET socket)
{
    if (socket == INVALID_SOCKET)
    {
        return;
    }

#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool _SetNonBlocking(SOCKET socket)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags != -1 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

unsigned int _WorkerThreadCount()
{
    if (const char* value = std::getenv("HTTP_SERVER_WORKER_THREADS"); value != nullptr && *value != '\0')
    {
        try
        {
            return std::max(1u, static_cast<unsigned int>(std::stoul(value)));
        }
        catch (...)
        {
        }
    }

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return hardwareThreads == 0 ? 4u : hardwareThreads;
}
} // namespace

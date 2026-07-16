#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <stdexcept>
#include <test_curl/kernel/ConnectionPool.hpp>
#include <test_curl/kernel/HttpRoute.hpp>
#include <test_curl/kernel/NetworkSocket.hpp>
#include <test_curl/kernel/ScopeGuard.hpp>
#include <test_curl/kernel/SocketConnection.hpp>
#include <thread>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const struct sockaddr_in* addr);
}

Result<void, DefaultErrorType> NetworkSocket::start()
{
    if (m_running.exchange(true))
    {
        return Error(DefaultErrorType::AlreadyRunning, "Socket is already running");
    }

    if (!port())
    {
        m_running = false;
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Impossible to initalized the socket because the port is not set"));
    }

#ifdef _WIN32
    WSADATA data;
    int error = WSAStartup(WINSOCK_VERSION, &data);
    if (error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to init the socket : [{}] {}", error, WSAGetLastError()));
    }
#else
    // We no longer reserve fds 0,1,2 since it breaks output
    // The accept() will use whatever fds are available
#endif

    m_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (m_handle == INVALID_SOCKET)
    {
        m_running = false;
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to create the socket : {}", WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to create the socket : {}", strerror(errno)));
#endif
    }

#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(m_handle, FIONBIO, &mode) != 0)
    {
        m_running = false;
        closesocket(m_handle);
        m_handle = INVALID_SOCKET;

        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to set server socket non-blocking : {}", WSAGetLastError()));
    }
#else
    int flags = fcntl(m_handle, F_GETFL, 0);
    fcntl(m_handle, F_SETFL, flags | O_NONBLOCK);
#endif

    int opt = 1;
#ifdef _WIN32
    setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    if (m_sourceData.sin_family == 0)
    {
        m_sourceData.sin_family = AF_INET;
        m_sourceData.sin_addr.s_addr = INADDR_ANY;
    }

    int errorBind = bind(m_handle, (struct sockaddr*)&m_sourceData, sizeof(m_sourceData));

    if (errorBind != 0)
    {
        m_running = false;
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to bind on {}:{} : [{}] {}", ip().DataOr("0.0.0.0"), port().DataOr(0), errorBind,
                                 WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to bind on {}:{} : [{}] {}", ip().DataOr("0.0.0.0"), port().DataOr(0), errorBind,
                                 strerror(errno)));
#endif
    }

    std::cout << std::format("Server listening on {}:{}\n", ip().DataOr("0.0.0.0").c_str(), port().Data());
    if (!m_listen())
    {
        m_running = false;
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to listen on {}:{} : [{}] {}", ip().DataOr("0.0.0.0"), port().DataOr(0),
                                 errorBind, WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to listen on {}:{} : [{}] {}", ip().DataOr("0.0.0.0"), port().DataOr(0),
                                 errorBind, strerror(errno)));
#endif
    }

#ifndef _WIN32
    int epollFd = epoll_create1(0);
    if (epollFd == -1)
    {
        m_running = false;
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to create epoll : {}", strerror(errno)));
    }

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Edge-triggered mode
    event.data.fd = m_handle;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, m_handle, &event) == -1)
    {
        close(epollFd);
        m_running = false;
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to add epoll event : {}", strerror(errno)));
    }

    struct epoll_event events[64];
    while (m_running)
    {
        int numEvents = epoll_wait(epollFd, events, 64, 100);
        if (numEvents == -1)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < numEvents; ++i)
        {
            if (events[i].data.fd == m_handle)
            {
                if (events[i].events & EPOLLERR)
                {
                    int error = 0;
                    socklen_t len = sizeof(error);
                    if (getsockopt(m_handle, SOL_SOCKET, SO_ERROR, &error, &len) == 0)
                    {
                        std::cerr << "Socket error: " << error << " (" << strerror(error) << ")\n";
                    }
                }
                m_connect();
            }
        }
    }
    close(epollFd);
#else
    while (m_running)
    {
        auto isConnected = m_connect();
        if (!isConnected)
        {
            std::string errorMsg = isConnected.GetError().GetFormatedError();
            if (errorMsg.find("Resource temporarily unavailable") == std::string::npos)
            {
                std::cerr << errorMsg << '\n';
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#endif

    m_running = false;
    return {};
}

Result<void, DefaultErrorType> NetworkSocket::startAsync()
{
    if (m_serverThread.joinable())
    {
        return Error(DefaultErrorType::AlreadyRunning, "Socket thread is already running");
    }

    m_serverThread = std::jthread(
        [this]
        {
            auto result = start();
            if (!result)
            {
                std::cerr << result.GetError().GetFormatedError() << '\n';
            }
        });

    return {};
}

void NetworkSocket::maxConnections(uint32_t max)
{
    m_maxConnections = max;
}

uint32_t NetworkSocket::maxConnections() const
{
    return m_maxConnections;
}

Result<void, DefaultErrorType> NetworkSocket::stop()
{
    m_running = false;

    if (m_pool)
    {
        m_pool->stop();
    }

    if (m_handle == INVALID_SOCKET)
    {
        return {};
    }

#ifdef _WIN32
    int shutdownResult = shutdown(m_handle, SD_BOTH);
    if (shutdownResult != 0)
    {
        int wsaError = WSAGetLastError();

        // Sur une listening socket, ce n'est pas forcément bloquant.
        if (wsaError != WSAENOTCONN && wsaError != WSAEINVAL)
        {
            std::cerr << std::format("Warning: shutdown main socket failed: {}\n", wsaError);
        }
    }

    if (int error = closesocket(m_handle); error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to close the main socket : [{}] {}", error, WSAGetLastError()));
    }

    m_handle = INVALID_SOCKET;
#else
    if (shutdown(m_handle, SHUT_RDWR) != 0)
    {
        if (errno != ENOTCONN)
        {
            return Error(DefaultErrorType::NotSpecialized,
                         std::format("Fail to shutdown the main socket : [{}] {}", errno, strerror(errno)));
        }
    }

    if (close(m_handle) != 0)
    {
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to disconnect the main socket : [{}] {}", errno, strerror(errno)));
    }
    m_handle = INVALID_SOCKET;
#endif

    if (m_serverThread.joinable() && m_serverThread.get_id() != std::this_thread::get_id())
    {
        m_serverThread.request_stop();
        m_serverThread.join();
    }

    return {};
}

void NetworkSocket::ip(std::string_view ip)
{
    m_sourceData.sin_family = ip.find(":") == std::string::npos ? AF_INET : AF_INET6;
    inet_pton(m_sourceData.sin_family, ip.data(), &(m_sourceData.sin_addr));
}

Result<std::string, DefaultErrorType> NetworkSocket::ip() const
{
    return _GetIpFromSockaddr(&m_sourceData);
}

void NetworkSocket::port(uint32_t port)
{
    m_sourceData.sin_port = htons(port);
}

Result<uint32_t, DefaultErrorType> NetworkSocket::port() const
{
    if (m_sourceData.sin_port == 0)
    {
        return Error(DefaultErrorType::NotSpecialized, "Port is not set");
    }

    return ntohs(m_sourceData.sin_port);
}

NetworkSocket::~NetworkSocket()
{
    stop();
}

bool NetworkSocket::m_listen()
{
    return listen(m_handle, 20) == 0;
}

Result<void, DefaultErrorType> NetworkSocket::m_connect()
{
    if (!m_pool)
    {
        return Error(DefaultErrorType::NotSpecialized, "Pool unitialized");
    }

    if (m_currentConnections >= m_maxConnections)
    {
        return {};
    }

    struct sockaddr_in clientData;
    socklen_t clientSize = sizeof(clientData);
    // Reset clientData to ensure it's zeroed out
    memset(&clientData, 0, sizeof(clientData));
    clientSize = sizeof(clientData);

    // Clear any previous errno
    errno = 0;

    // Check socket state before accept
    int sockError = 0;
    socklen_t sockErrorLen = sizeof(sockError);
    getsockopt(m_handle, SOL_SOCKET, SO_ERROR, (char*)&sockError, &sockErrorLen);

#ifdef _WIN32
    SOCKET connection = accept(m_handle, (sockaddr*)&clientData, &clientSize);

    if (connection == INVALID_SOCKET)
    {
        const int error = WSAGetLastError();

        if (error == WSAEWOULDBLOCK)
        {
            return {};
        }

        if (!m_running)
        {
            return {};
        }

        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to accept the connection : {}", error));
    }
#else
    SOCKET connection = accept4(m_handle, (sockaddr*)&clientData, &clientSize, SOCK_CLOEXEC);
    if (connection == INVALID_SOCKET)
    {
        connection = accept(m_handle, (sockaddr*)&clientData, &clientSize);
        if (connection >= 0)
        {
            fcntl(connection, F_SETFD, FD_CLOEXEC);
        }
    }
#endif

    // Validate socket descriptor - reject fds reserved for stdin/stdout/stderr
    // These can cause issues when running in background
    if (connection >= 0 && connection < 3)
    {
#ifdef _WIN32
        closesocket(connection);
#else
        close(connection);
#endif
        return {};
    }

    if (connection == INVALID_SOCKET)
    {
#ifndef _WIN32
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return {};
        }
#endif
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to accept the connection1 : {}", WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to accept the connection : {}", strerror(errno)));
#endif
    }

    // Validate socket descriptor - only reject invalid values
    if (connection < 0)
    {
        return {};
    }

    // Set accepted socket to blocking mode
    // The accepted socket inherits O_NONBLOCK from the listening socket,
    // so we need to explicitly set it to blocking
#ifdef _WIN32
    u_long mode = 0;
    ioctlsocket(connection, FIONBIO, &mode);
    // Set receive timeout to prevent immediate failures
    int timeout = 30000; // 30 seconds
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    int flags = fcntl(connection, F_GETFL, 0);
    if (flags != -1)
    {
        fcntl(connection, F_SETFL, flags & ~O_NONBLOCK);
    }
#endif

    m_currentConnections++;
    auto connPtr = std::make_shared<SocketConnection>(connection, clientData);
    connPtr->onClose([this]() { m_currentConnections--; });
    m_pool->m_push(std::move(*connPtr));
    return {};
}

namespace
{
Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const struct sockaddr_in* addr)
{
    std::string ip;
    ip.resize(48);
    if (inet_ntop(addr->sin_family, &addr->sin_addr, ip.data(), ip.size()) == NULL)
    {
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to get ip : {}", WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to get ip : {}", strerror(errno)));
#endif
    }
    ip.resize(strlen(ip.c_str()));
    return ip;
}
} // namespace

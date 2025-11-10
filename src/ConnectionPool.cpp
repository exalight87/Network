#include <ConnectionPool.hpp>

#include <chrono>
#include <iostream>
#include <utility>

ConnectionPool::ConnectionPool(std::function<void(std::stop_token, std::shared_ptr<SocketConnection>)> callable)
    : m_entryPoint(std::move(callable))
    , m_cleaner([this](std::stop_token stopToken)
        {
            using namespace std::chrono_literals;
            while (!stopToken.stop_requested())
            {
                cleanupClosedConnections();
                std::this_thread::sleep_for(1s);
            }

            cleanupClosedConnections();
        })
{
}

void ConnectionPool::m_push(SocketConnection&& connection)
{
    auto connPtr = std::make_shared<SocketConnection>(std::move(connection));
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connections.push_back(connPtr);
    m_listeners.emplace_back(m_entryPoint, connPtr);
}

void ConnectionPool::cleanupClosedConnections()
{
    std::scoped_lock<std::mutex> lock(m_mutex);

    auto connectionIt = m_connections.begin();
    auto listenerIt = m_listeners.begin();
    while (connectionIt != m_connections.end())
    {
        if ((*connectionIt)->isClosed())
        {
            listenerIt->request_stop();
            listenerIt = m_listeners.erase(listenerIt);
            connectionIt = m_connections.erase(connectionIt);
        }
        else
        {
            ++connectionIt;
            ++listenerIt;
        }
    }
}

void ConnectionPool::stop()
{
    m_cleaner.request_stop();

    std::vector<std::shared_ptr<SocketConnection>> connectionsSnapshot;
    {
        std::scoped_lock<std::mutex> lock(m_mutex);
        for (auto& listener : m_listeners)
        {
            listener.request_stop();
        }

        connectionsSnapshot = m_connections;
        m_connections.clear();
        m_listeners.clear();
    }

    for (auto& connection : connectionsSnapshot)
    {
        if (auto result = connection->disconnect(); !result)
        {
            std::cerr << result.error().GetFormatedError() << '\n';
        }
    }
}

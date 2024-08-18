#include <ConnectionPool.hpp>
#include <iostream>
#include <chrono>
#include <any>

ConnectionPool::ConnectionPool(std::function< void(std::stop_token, std::shared_ptr<SocketConnection>) > callable)
    : m_entryPoint(callable) 
{
    m_cleaner = std::jthread([this](std::stop_token stopToken)
        {
            auto removeClosedConnections = [this]() {
                auto removePredicate = [this](const auto& connection, std::size_t idx) {
                    if (connection->isClosed()) {
                        m_listeners[idx].request_stop();
                        return true;
                    }
                    return false;
                    };

                for (std::size_t idx = 0; idx < m_connections.size(); ++idx) {
                    if (removePredicate(m_connections[idx], idx)) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_connections.erase(m_connections.begin() + idx);
                        m_listeners.erase(m_listeners.begin() + idx);
                        --idx; // Adjust index after erase
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
}

void ConnectionPool::m_push(SocketConnection&& connection)
{   
    auto connPtr = std::make_shared<SocketConnection>(std::move(connection));
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connections.push_back(connPtr);
    m_listeners.emplace_back(m_entryPoint, connPtr);
}

void ConnectionPool::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& connection : m_connections)
    {
        connection->disconnect();
    }
}
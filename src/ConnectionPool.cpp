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
                std::vector<std::size_t> toRemove;
                
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (std::size_t idx = 0; idx < m_connections.size(); ++idx) {
                        if (m_connections[idx]->isClosed()) {
                            toRemove.push_back(idx);
                        }
                    }
                }

                for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
                    std::size_t idx = *it;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (idx < m_listeners.size()) {
                        m_listeners[idx].request_stop();
                    }
                    if (idx < m_connections.size()) {
                        std::swap(m_connections[idx], m_connections.back());
                        m_connections.pop_back();
                        if (idx < m_listeners.size()) {
                            std::swap(m_listeners[idx], m_listeners.back());
                            m_listeners.pop_back();
                        }
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

    for (auto& listener : m_listeners) {
        listener.request_stop();
    }
    m_listeners.clear();
    
    for (auto& connection : m_connections)
    {
        connection->disconnect();
    }
    m_connections.clear();
}

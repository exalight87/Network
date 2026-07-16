#include <ConnectionPool.hpp>
#include <iostream>
#include <chrono>
#include <any>

ConnectionPool::ConnectionPool(std::function< void(std::stop_token, std::shared_ptr<SocketConnection>) > callable)
    : m_entryPoint(callable) 
{
    // Create thread pool based on hardware concurrency
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;

    for (unsigned int i = 0; i < numThreads; ++i)
    {
        m_workers.emplace_back([this](std::stop_token stopToken) { m_worker(stopToken); });
    }

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
                    if (idx < m_connections.size()) {
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
            m_workCondition.wait(lock, [this, &stopToken]() {
                return !m_workQueue.empty() || stopToken.stop_requested() || m_stop;
            });

            if (stopToken.stop_requested() || m_stop) break;

            if (!m_workQueue.empty())
            {
                connection = m_workQueue.front();
                m_workQueue.pop();
            }
        }

        if (connection && !connection->isClosed())
        {
            m_entryPoint(stopToken, connection);
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

void ConnectionPool::stop()
{
    m_stop = true;
    m_workCondition.notify_all();
    
    for (auto& connection : m_connections)
    {
        connection->disconnect();
    }
    
    m_workers.clear();
    m_connections.clear();
}

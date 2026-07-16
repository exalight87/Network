#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <test_curl/kernel/SocketConnection.hpp>
#include <thread>
#include <utility>
#include <vector>

class NetworkSocket;

class ConnectionPool
{
    friend NetworkSocket;

  public:
    ConnectionPool(std::function<void(std::stop_token, std::shared_ptr<SocketConnection>)> callable);
    ~ConnectionPool();
    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_connections.size();
    };
    void stop();

  private:
    void m_push(SocketConnection&& connection);
    void m_worker(std::stop_token stopToken);

  private:
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<SocketConnection>> m_connections;
    std::queue<std::shared_ptr<SocketConnection>> m_workQueue;
    std::condition_variable m_workCondition;
    std::vector<std::jthread> m_workers;
    std::jthread m_cleaner;
    std::function<void(std::stop_token, std::shared_ptr<SocketConnection>)> m_entryPoint;
    std::atomic_bool m_stop{false};
};

#pragma once
#include <thread>
#include <utility>
#include <functional>
#include <stop_token>
#include <thread>
#include <SocketConnection.hpp>
#include <mutex>

class NetworkSocket;

class ConnectionPool
{
    friend NetworkSocket;

public:
    ConnectionPool(std::function< void(std::stop_token, std::shared_ptr<SocketConnection>) > callable);
    std::size_t size() const { 
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_connections.size(); 
    };
    void stop();

private:
    void m_push(SocketConnection&& connection);

private:
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<SocketConnection>> m_connections;
    std::vector<std::jthread> m_listeners;
    std::jthread m_cleaner;
    std::function< void(std::stop_token, std::shared_ptr<SocketConnection>) > m_entryPoint;
};
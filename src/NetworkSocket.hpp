#pragma once
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <string_view>
#include <span>
#include <array>
#include <Result.hpp>
#include <thread>
#include <functional>
#include <ConnectionPool.hpp>

struct HttpRoute;

class NetworkSocket
{
public:
    NetworkSocket() = default;
    NetworkSocket(const NetworkSocket&) = default;
    NetworkSocket& operator=(const NetworkSocket&) = default;
    NetworkSocket(NetworkSocket&&) noexcept = default;
    NetworkSocket& operator=(NetworkSocket&&) noexcept = default;
    ~NetworkSocket();

    void ip( std::string_view ip );
    Result<std::string, DefaultErrorType> ip() const;
    void port( uint32_t port );
    Result<uint32_t, DefaultErrorType> port() const;

    // Blocking infinite loop responsible for handling connections
    [[nodiscard]] Result<void, DefaultErrorType> start();
    Result<void, DefaultErrorType> stop();

protected:
    bool m_listen();
    Result<void, DefaultErrorType> m_connect();

    SOCKADDR_IN m_sourceData;
    SOCKET m_handle;
    std::unique_ptr<ConnectionPool> m_pool;
};
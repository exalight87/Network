#pragma once
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <string_view>
#include <span>
#include <array>
#include <Result.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

struct HttpRoute;
class ConnectionPool;

class NetworkSocket
{
public:
    NetworkSocket() = default;
    NetworkSocket(const NetworkSocket&) = default;
    NetworkSocket& operator=(const NetworkSocket&) = default;
    NetworkSocket(NetworkSocket&&) noexcept = default;
    NetworkSocket& operator=(NetworkSocket&&) noexcept = default;
    ~NetworkSocket();

    void ip(std::string_view ip);
    [[nodiscard]] Result<std::string, DefaultErrorType> ip() const;
    void port(uint32_t port);
    [[nodiscard]] Result<uint32_t, DefaultErrorType> port() const;

    // Blocking infinite loop responsible for handling connections
    [[nodiscard]] Result<void, DefaultErrorType> start();
    Result<void, DefaultErrorType> stop();

protected:
    Result<void, DefaultErrorType> m_listen();
    Result<void, DefaultErrorType> m_connect();

    SOCKADDR_IN m_sourceData{};
    SOCKET m_handle{INVALID_SOCKET};
    std::unique_ptr<ConnectionPool> m_pool;
};

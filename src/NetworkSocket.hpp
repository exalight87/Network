#pragma once
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif
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

    // Set maximum concurrent connections (default: 1000)
    void maxConnections(uint32_t max);
    uint32_t maxConnections() const;

    // Blocking infinite loop responsible for handling connections
    [[nodiscard]] Result<void, DefaultErrorType> start();
    Result<void, DefaultErrorType> stop();

protected:
    bool m_listen();
    Result<void, DefaultErrorType> m_connect();

#ifdef _WIN32
    SOCKADDR_IN m_sourceData;
#else
    struct sockaddr_in m_sourceData;
#endif
    SOCKET m_handle;
    std::unique_ptr<ConnectionPool> m_pool;
    uint32_t m_maxConnections = 1000;
    std::atomic<uint32_t> m_currentConnections = 0;
};
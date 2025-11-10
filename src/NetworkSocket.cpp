#include <NetworkSocket.hpp>
#include <stdexcept>
#include <array>
#include <format>
#include <WS2tcpip.h>
#include <iostream>
#include <optional>
#include <string>
#include <HttpRoute.hpp>
#include <chrono>
#include <SocketConnection.hpp>
#include <ConnectionPool.hpp>

namespace
{
    
    Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const SOCKADDR_IN* addr);
}


Result<void, DefaultErrorType> NetworkSocket::start()
{
    auto rPort = port();
    if (!rPort)
    {
        return Result<void, DefaultErrorType>{rPort.error()};
    }
    const auto portValue = rPort.value();

    auto rIp = ip();
    const auto ipValue = rIp ? rIp.value() : std::string{"0.0.0.0"};

    WSADATA data{};
    if (const int startupError = WSAStartup(WINSOCK_VERSION, &data); startupError != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to init the socket : [{}] {}", startupError, WSAGetLastError()));
    }

    m_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_handle == INVALID_SOCKET)
    {
        WSACleanup();
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to create the socket : {}", WSAGetLastError()));
    }

    if (!rIp)
    {
        m_sourceData.sin_addr.s_addr = INADDR_ANY;
        m_sourceData.sin_family = AF_INET;
    }

    if (const int bindError = bind(m_handle, reinterpret_cast<sockaddr*>(&m_sourceData), sizeof(m_sourceData)); bindError != 0)
    {
        const auto wsaError = WSAGetLastError();
        closesocket(m_handle);
        m_handle = INVALID_SOCKET;
        WSACleanup();
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to bind on {}:{} : [{}] {}", ipValue, portValue, bindError, wsaError));
    }

    std::cout << std::format("Server listening on {}:{}\n", ipValue, portValue);

    if (auto listenResult = m_listen(); !listenResult)
    {
        auto error = listenResult.error();
        closesocket(m_handle);
        m_handle = INVALID_SOCKET;
        WSACleanup();
        return Result<void, DefaultErrorType>{std::move(error)};
    }

    while (true)
    {
        if (auto isConnected = m_connect(); !isConnected)
        {
            std::cerr << isConnected.error().GetFormatedError() << '\n';
        }
    }

    return {};
}


void NetworkSocket::ip(std::string_view ip)
{
    if (ip.find(':') != std::string::npos)
    {
        throw std::runtime_error("IPv6 addresses are not supported by this socket implementation");
    }

    m_sourceData.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip.data(), &(m_sourceData.sin_addr)) != 1)
    {
        throw std::runtime_error(std::format("Invalid ip address provided: {}", ip));
    }
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


Result<void, DefaultErrorType> NetworkSocket::m_listen()
{
    if (listen(m_handle, SOMAXCONN) == 0)
    {
        return {};
    }

    const auto wsaError = WSAGetLastError();
    return Error(DefaultErrorType::NotSpecialized, std::format("Fail to listen on the socket : {}", wsaError));
}


Result<void, DefaultErrorType> NetworkSocket::m_connect()
{
    if (!m_pool)
    {
        return Error(DefaultErrorType::NotSpecialized, "Pool unitialized");
    }
    SOCKADDR_IN clientData;
    int clientSize = sizeof(clientData);
    SOCKET connection = accept(m_handle, reinterpret_cast<sockaddr*>(&clientData), &clientSize);
    if (connection == INVALID_SOCKET)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to accept the connection : {}", WSAGetLastError()));
    }

    m_pool->m_push({ connection, clientData });
    return {};
}


Result<void, DefaultErrorType> NetworkSocket::stop()
{
    if (m_pool)
    {
        m_pool->stop();
    }

    std::optional<Error<DefaultErrorType>> firstError;

    if (m_handle != INVALID_SOCKET)
    {
        if (const int error = shutdown(m_handle, SD_BOTH); error != 0)
        {
            firstError.emplace(DefaultErrorType::NotSpecialized, std::format("Fail to shutdown the main socket : [{}] {}", error, WSAGetLastError()));
        }

        if (const int error = closesocket(m_handle); error != 0 && !firstError)
        {
            firstError.emplace(DefaultErrorType::NotSpecialized, std::format("Fail to disconnect the main socket : [{}] {}", error, WSAGetLastError()));
        }

        m_handle = INVALID_SOCKET;
    }

    if (const int error = WSACleanup(); error != 0 && error != WSANOTINITIALISED && !firstError)
    {
        firstError.emplace(DefaultErrorType::NotSpecialized, std::format("Fail to free winsock : [{}] {}", error, WSAGetLastError()));
    }

    if (firstError)
    {
        return Result<void, DefaultErrorType>{std::move(*firstError)};
    }

    return {};
}

namespace {

    
    Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const SOCKADDR_IN* addr)
    {
        constexpr std::size_t bufferSize = INET6_ADDRSTRLEN;
        std::array<char, bufferSize> buffer{};
        if (!inet_ntop(addr->sin_family, reinterpret_cast<const void*>(&addr->sin_addr), buffer.data(), static_cast<socklen_t>(buffer.size())))
        {
            return Error(DefaultErrorType::NotSpecialized, std::format("Fail to get ip : {}", WSAGetLastError()));
        }
        return std::string{buffer.data()};
    }
}

#include <NetworkSocket.hpp>
#include <stdexcept>
#include <format>
#include <WS2tcpip.h>
#include <iostream>
#include <HttpRoute.hpp>
#include <chrono>
#include <SocketConnection.hpp>
#include <ConnectionPool.hpp>
#include <ScopeGuard.hpp>

namespace
{
    
    Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const SOCKADDR_IN* addr);
}


Result<void, DefaultErrorType> NetworkSocket::start()
{
    if (!port())
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Impossible to initalized the socket because the port is not set"));
    }

    WSADATA data;
    int error = WSAStartup(WINSOCK_VERSION, &data);
    if (error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to init the socket : [{}] {}", error, WSAGetLastError()));
    }

    m_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_handle == INVALID_SOCKET)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to create the socket : {}", WSAGetLastError()));
    }

    if (!ip())
    {
        m_sourceData.sin_addr.s_addr = INADDR_ANY;
        m_sourceData.sin_family = AF_INET;
    }

    error = bind(m_handle, (struct sockaddr*)&m_sourceData, sizeof(m_sourceData));

    if (error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to bind on {}:{} : [{}] {}", ip().Data(), m_sourceData.sin_port, error, WSAGetLastError()));
    }

    std::cout << std::format("Server listening on {}:{}\n", ip().Data().c_str(), port().Data());
    if (!m_listen())
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to listen on {}:{} : [{}] {}", ip().Data(), m_sourceData.sin_port, error, WSAGetLastError()));
    }

    while (true)
    {
        auto isConnected = m_connect();
        if (!isConnected)
        {
            std::cerr << isConnected.Error().GetFormatedError() << '\n';
        }
    }

    return {};
}


void NetworkSocket::ip(std::string_view ip)
{
    m_sourceData.sin_family = ip.find(":") == std::string::npos ? AF_INET : AF_INET6;
    inet_pton(m_sourceData.sin_family, ip.data(), &(m_sourceData.sin_addr));
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

    return ntohs( m_sourceData.sin_port );
}


NetworkSocket::~NetworkSocket()
{
    stop();
}


bool NetworkSocket::m_listen()
{
    return listen(m_handle, 20) == 0;
}


Result<void, DefaultErrorType> NetworkSocket::m_connect()
{
    if ( !m_pool )
    {
        return Error(DefaultErrorType::NotSpecialized, "Pool unitialized");
    }
    SOCKADDR_IN clientData;
    int clientSize = sizeof(clientData);
    SOCKET connection = accept(m_handle, (sockaddr*) &clientData, &clientSize);
    if (connection == INVALID_SOCKET)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to accept the connection1 : {}", WSAGetLastError()));
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

    if (int error = shutdown(m_handle, 2); error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to shutdown the main socket : [{}] {}", error, WSAGetLastError()) );
    }

    if (int error = closesocket(m_handle); error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to disconnect the main socket : [{}] {}", error, WSAGetLastError()));
    }

    if (int error = WSACleanup(); error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to free winsock : [{}] {}", error, WSAGetLastError()));
    }

    return {};
}

namespace {

    
    Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const SOCKADDR_IN* addr )
    {
        std::string ip;
        ip.reserve(addr->sin_family == AF_INET ? 16 : 48);
        if (inet_ntop(addr->sin_family, &addr->sin_addr, ip.data(), ip.capacity()) == NULL)
        {
            return Error(DefaultErrorType::NotSpecialized, std::format("Fail to get ip : {}", WSAGetLastError()));
        }
        return ip;
    }
}
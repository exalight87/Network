#include <NetworkSocket.hpp>
#include <stdexcept>
#include <format>
#include <iostream>
#include <HttpRoute.hpp>
#include <chrono>
#include <SocketConnection.hpp>
#include <ConnectionPool.hpp>
#include <ScopeGuard.hpp>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#endif

namespace
{
    
    Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const struct sockaddr_in* addr);
}


Result<void, DefaultErrorType> NetworkSocket::start()
{
    if (!port())
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Impossible to initalized the socket because the port is not set"));
    }

#ifdef _WIN32
    WSADATA data;
    int error = WSAStartup(WINSOCK_VERSION, &data);
    if (error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to init the socket : [{}] {}", error, WSAGetLastError()));
    }
#endif

    m_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (m_handle == INVALID_SOCKET)
    {
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to create the socket : {}", WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to create the socket : {}", strerror(errno)));
#endif
    }

#ifndef _WIN32
    int flags = fcntl(m_handle, F_GETFL, 0);
    fcntl(m_handle, F_SETFL, flags | O_NONBLOCK);
#endif

    int opt = 1;
#ifdef _WIN32
    setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    if (!ip())
    {
        m_sourceData.sin_addr.s_addr = INADDR_ANY;
        m_sourceData.sin_family = AF_INET;
    }

    int error = bind(m_handle, (struct sockaddr*)&m_sourceData, sizeof(m_sourceData));

    if (error != 0)
    {
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to bind on {}:{} : [{}] {}", ip().Data(), m_sourceData.sin_port, error, WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to bind on {}:{} : [{}] {}", ip().Data(), m_sourceData.sin_port, error, strerror(errno)));
#endif
    }

    std::cout << std::format("Server listening on {}:{}\n", ip().DataOr("0.0.0.0").c_str(), port().Data());
    if (!m_listen())
    {
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to listen on {}:{} : [{}] {}", ip().Data(), m_sourceData.sin_port, error, WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to listen on {}:{} : [{}] {}", ip().DataOr("0.0.0.0"), m_sourceData.sin_port, error, strerror(errno)));
#endif
    }

    while (true)
    {
        auto isConnected = m_connect();
        if (!isConnected)
        {
            std::cerr << isConnected.GetError().GetFormatedError() << '\n';
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
    struct sockaddr_in clientData;
    socklen_t clientSize = sizeof(clientData);
    SOCKET connection = accept(m_handle, (sockaddr*) &clientData, &clientSize);
    if (connection == INVALID_SOCKET)
    {
#ifndef _WIN32
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return {};
        }
#endif
#ifdef _WIN32
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to accept the connection1 : {}", WSAGetLastError()));
#else
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to accept the connection : {}", strerror(errno)));
#endif
    }

#ifndef _WIN32
    int flags = fcntl(connection, F_GETFL, 0);
    fcntl(connection, F_SETFL, flags | O_NONBLOCK);
#endif

    m_pool->m_push({ connection, clientData });
    return {};
}


Result<void, DefaultErrorType> NetworkSocket::stop()
{
    if (m_pool)
    {
        m_pool->stop();
    }

#ifdef _WIN32
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
#else
    if (shutdown(m_handle, SHUT_RDWR) != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to shutdown the main socket : [{}] {}", errno, strerror(errno)));
    }

    if (close(m_handle) != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to disconnect the main socket : [{}] {}", errno, strerror(errno)));
    }
#endif

    return {};
}

namespace {

    
    Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const struct sockaddr_in* addr )
    {
        std::string ip;
        ip.resize(48);
        if (inet_ntop(addr->sin_family, &addr->sin_addr, ip.data(), ip.size()) == NULL)
        {
#ifdef _WIN32
            return Error(DefaultErrorType::NotSpecialized, std::format("Fail to get ip : {}", WSAGetLastError()));
#else
            return Error(DefaultErrorType::NotSpecialized, std::format("Fail to get ip : {}", strerror(errno)));
#endif
        }
        ip.resize(strlen(ip.c_str()));
        return ip;
    }
}
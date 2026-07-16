#include <format>
#include <iostream>
#include <test_curl/kernel/SocketConnection.hpp>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
int send_windows(SOCKET s, std::span<const char> buf, int flags)
{
    return send(s, buf.data(), static_cast<int>(buf.size()), 0);
}
#endif

Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const struct sockaddr_in* addr);
} // namespace

Result<void, DefaultErrorType> SocketConnection::receive(std::vector<char>& data)
{
    if (m_closeRequested || m_handle == INVALID_SOCKET)
    {
        return Error(DefaultErrorType::NotSpecialized, "Connection closed");
    }

#ifdef _WIN32
    unsigned long available = 0;
    if (ioctlsocket(m_handle, FIONREAD, &available) != 0)
    {
        const int error = WSAGetLastError();

        if (error == WSAENOTSOCK || error == WSAEINVAL)
        {
            return Error(DefaultErrorType::NotSpecialized, "Connection closed");
        }

        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to check available data : {}", error));
    }

    if (available == 0)
    {
        return {};
    }
#endif

    while (true)
    {
        int bytesReceived = recv(m_handle, m_recvBuffer.data(), static_cast<int>(m_recvBuffer.size()), 0);

        if (bytesReceived > 0)
        {
            data.insert(data.end(), m_recvBuffer.begin(), m_recvBuffer.begin() + bytesReceived);
            return {};
        }

        if (bytesReceived == 0)
        {
            disconnect();
            return Error(DefaultErrorType::NotSpecialized, "Connection closed");
        }

#ifdef _WIN32
        int wsaError = WSAGetLastError();

        if (wsaError == WSAEWOULDBLOCK)
        {
            return {};
        }

        if (wsaError == WSAENOTSOCK || wsaError == WSAECONNRESET || wsaError == WSAESHUTDOWN)
        {
            return Error(DefaultErrorType::NotSpecialized, "Connection closed");
        }

        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to receive data : {}", wsaError));
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return {};
        }

        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to receive data : {}", strerror(errno)));
#endif
    }
}

[[nodiscard]] Result<void, DefaultErrorType> SocketConnection::send(std::span<const char> data)
{
    m_nbRequest++;

#ifdef _WIN32
    if (int error = send_windows(m_handle, data, 0); error < 0)
    {
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to send the data : [{}] {}", error, WSAGetLastError()));
    }
#else
    ssize_t totalSent = 0;
    while (totalSent < static_cast<ssize_t>(data.size()))
    {
        ssize_t sent = ::send(m_handle, data.data() + totalSent, data.size() - totalSent, 0);
        if (sent < 0)
        {
            return Error(DefaultErrorType::NotSpecialized,
                         std::format("Fail to send the data : [{}] {}", sent, strerror(errno)));
        }
        totalSent += sent;
    }
#endif

    return {};
}

Result<void, DefaultErrorType> SocketConnection::disconnect()
{
    if (m_handle == INVALID_SOCKET)
    {
        return {};
    }

#ifdef _WIN32
    shutdown(m_handle, SD_BOTH);
    if (int error = closesocket(m_handle); error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to disconnect the socket : [{}] {}", error, WSAGetLastError()));
    }
#else
    if (shutdown(m_handle, SHUT_RDWR) != 0 && errno != ENOTCONN)
    {
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to shutdown the socket : [{}] {}", errno, strerror(errno)));
    }

    if (close(m_handle) != 0)
    {
        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to disconnect the socket : [{}] {}", errno, strerror(errno)));
    }
#endif

    m_handle = INVALID_SOCKET;
    if (m_onClose)
        m_onClose();
    return {};
}

SocketConnection::SocketConnection(SOCKET handle, struct sockaddr_in clientData)
    : m_clientData(clientData), m_handle(handle), m_nbRequest(0) {};

Result<std::string, DefaultErrorType> SocketConnection::ip() const
{
    return _GetIpFromSockaddr(&m_clientData);
}

Result<uint32_t, DefaultErrorType> SocketConnection::port() const
{
    return ntohs(m_clientData.sin_port);
}

namespace
{

Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const struct sockaddr_in* addr)
{
    std::string ip;
    ip.reserve(addr->sin_family == AF_INET ? 16 : 48);
    if (inet_ntop(addr->sin_family, &addr->sin_addr, ip.data(), ip.capacity()) == NULL)
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
} // namespace

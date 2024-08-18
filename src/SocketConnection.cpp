#include <SocketConnection.hpp>
#include <format>
#include <WS2tcpip.h>

namespace
{
    int send_windows(SOCKET s, std::span<const char> buf, int flags)
    {
        return send(s, buf.data(), static_cast<int>(buf.size()), 0);
    }

    
    Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const SOCKADDR_IN* addr);
}


Result<void, DefaultErrorType> SocketConnection::receive(std::vector<char>& data)
{
    unsigned long l;
    while (ioctlsocket(m_handle, FIONREAD, &l) == 0 && l != 0)
    {
        int bytesReceived = recv(m_handle, m_recvBuffer.data(), static_cast<int>(m_recvBuffer.size()), 0);

        if (bytesReceived > 0)
        {
            data.insert(data.end(), m_recvBuffer.begin(), m_recvBuffer.begin() + bytesReceived);
        }
        else if (bytesReceived == 0) // The socket is closed
        {
            return {};
        }
        else if (bytesReceived != 0)
        {
            return Error(DefaultErrorType::NotSpecialized, std::format("Fail to disconnect the socket : [{}] {}", bytesReceived, WSAGetLastError()));
        }
    }
    return {};
}


Result<void, DefaultErrorType> SocketConnection::send(std::span<const char> data)
{
    m_nbRequest++;

    if (int error = send_windows(m_handle, data, 0); error < 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to send the data : [{}] {}", error, WSAGetLastError()));
    }

    return {};
}


Result<void, DefaultErrorType> SocketConnection::disconnect()
{
    if (m_handle == NULL)
    {
        return {};
    }

    if (int error = closesocket(m_handle); error != 0)
    {
        return Error(DefaultErrorType::NotSpecialized, std::format("Fail to disconnect the socket : [{}] {}", error, WSAGetLastError()));
    }

    m_handle = NULL;
    return {};
}


SocketConnection::SocketConnection(SOCKET handle, SOCKADDR_IN clientData)
    : m_handle(handle),
      m_clientData(clientData),
      m_nbRequest(0){};


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
    
    Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const SOCKADDR_IN* addr)
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
#include <format>
#include <iostream>
#include <cstdlib>
#include <climits>
#include <algorithm>
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
std::chrono::milliseconds _EnvDurationMs(const char* name, std::chrono::milliseconds defaultValue);
std::size_t _EnvSize(const char* name, std::size_t defaultValue);

#ifdef _WIN32
int send_windows(SOCKET s, std::span<const char> buf, int flags)
{
    return send(s, buf.data(), static_cast<int>(buf.size()), flags);
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

    const auto deadline = std::chrono::steady_clock::now() + writeTimeout();
    std::size_t totalSent = 0;

    while (totalSent < data.size())
    {
#ifdef _WIN32
        const auto remaining = data.size() - totalSent;
        const int chunkSize = static_cast<int>(std::min<std::size_t>(remaining, static_cast<std::size_t>(INT_MAX)));
        const int sent = send_windows(m_handle, data.subspan(totalSent, chunkSize), 0);
        if (sent > 0)
        {
            totalSent += static_cast<std::size_t>(sent);
            continue;
        }

        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return Error(DefaultErrorType::NotSpecialized, "Write timeout");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to send the data : [{}] {}", sent, error));
#else
        ssize_t sent = ::send(m_handle, data.data() + totalSent, data.size() - totalSent, MSG_NOSIGNAL);
        if (sent > 0)
        {
            totalSent += static_cast<std::size_t>(sent);
            continue;
        }

        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return Error(DefaultErrorType::NotSpecialized, "Write timeout");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (sent < 0 && errno == EINTR)
        {
            continue;
        }

        return Error(DefaultErrorType::NotSpecialized,
                     std::format("Fail to send the data : [{}] {}", sent, strerror(errno)));
#endif
    }

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

std::chrono::milliseconds SocketConnection::keepAliveTimeout() const
{
    return _EnvDurationMs("HTTP_SERVER_KEEP_ALIVE_TIMEOUT_MS", std::chrono::milliseconds(2000));
}

std::chrono::milliseconds SocketConnection::requestTimeout() const
{
    return _EnvDurationMs("HTTP_SERVER_REQUEST_TIMEOUT_MS", std::chrono::milliseconds(5000));
}

std::chrono::milliseconds SocketConnection::writeTimeout() const
{
    return _EnvDurationMs("HTTP_SERVER_WRITE_TIMEOUT_MS", std::chrono::milliseconds(5000));
}

std::chrono::seconds SocketConnection::timeout() const
{
    return std::chrono::duration_cast<std::chrono::seconds>(keepAliveTimeout());
}

std::size_t SocketConnection::maxRequest() const
{
    return _EnvSize("HTTP_SERVER_MAX_REQUESTS_PER_CONNECTION", 10000);
}

void SocketConnection::markRequestStarted()
{
    if (!m_hasRequestStarted)
    {
        m_requestStartedAt = std::chrono::steady_clock::now();
        m_hasRequestStarted = true;
    }
}

void SocketConnection::clearRequestStarted()
{
    m_hasRequestStarted = false;
}

bool SocketConnection::requestTimedOut(std::chrono::steady_clock::time_point now) const
{
    return m_hasRequestStarted && (now - m_requestStartedAt) > requestTimeout();
}

void SocketConnection::markIdle()
{
    m_idleSince = std::chrono::steady_clock::now();
}

bool SocketConnection::keepAliveTimedOut(std::chrono::steady_clock::time_point now) const
{
    return m_idleSince.time_since_epoch().count() != 0 && (now - m_idleSince) > keepAliveTimeout();
}

namespace
{

std::chrono::milliseconds _EnvDurationMs(const char* name, std::chrono::milliseconds defaultValue)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return defaultValue;
    }

    try
    {
        const auto parsed = std::stoll(value);
        if (parsed <= 0)
        {
            return defaultValue;
        }
        return std::chrono::milliseconds(parsed);
    }
    catch (...)
    {
        return defaultValue;
    }
}

std::size_t _EnvSize(const char* name, std::size_t defaultValue)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return defaultValue;
    }

    try
    {
        const auto parsed = std::stoull(value);
        return parsed == 0 ? defaultValue : static_cast<std::size_t>(parsed);
    }
    catch (...)
    {
        return defaultValue;
    }
}

Result<std::string, DefaultErrorType> _GetIpFromSockaddr(const struct sockaddr_in* addr)
{
    std::string ip;
    ip.resize(addr->sin_family == AF_INET ? 16 : 48);
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
} // namespace

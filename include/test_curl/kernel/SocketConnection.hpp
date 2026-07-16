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
#endif
#include <test_curl/kernel/Result.hpp>
#include <chrono>
#include <thread>
#include <utility>
#include <functional>
#include <array>
#include <span>

class NetworkSocket;

class SocketConnection
{
    friend NetworkSocket;
    friend class std::shared_ptr<SocketConnection>;

public:
    SocketConnection(const SocketConnection&) = delete;
    SocketConnection& operator=(const SocketConnection&) = delete;
    SocketConnection(SocketConnection&& other) noexcept
        : m_clientData(std::exchange(other.m_clientData, {})),
        m_handle(std::exchange(other.m_handle, INVALID_SOCKET)),
        m_nbRequest(std::exchange(other.m_nbRequest, 0)),
        m_closeRequested(std::exchange(other.m_closeRequested, false)),
        m_onClose(std::exchange(other.m_onClose, {})),
        m_recvBuffer(std::exchange(other.m_recvBuffer, {})),
        m_pendingData(std::exchange(other.m_pendingData, {})),
        m_requestStartedAt(std::exchange(other.m_requestStartedAt, {})),
        m_idleSince(std::exchange(other.m_idleSince, {})),
        m_hasRequestStarted(std::exchange(other.m_hasRequestStarted, false)) {};
    SocketConnection& operator=(SocketConnection&& other) noexcept
    {
        std::swap(m_clientData, other.m_clientData);
        std::swap(m_handle, other.m_handle);
        std::swap(m_nbRequest, other.m_nbRequest);
        std::swap(m_closeRequested, other.m_closeRequested);
        std::swap(m_onClose, other.m_onClose);
        std::swap(m_recvBuffer, other.m_recvBuffer);
        std::swap(m_pendingData, other.m_pendingData);
        std::swap(m_requestStartedAt, other.m_requestStartedAt);
        std::swap(m_idleSince, other.m_idleSince);
        std::swap(m_hasRequestStarted, other.m_hasRequestStarted);

        return *this;
    };

    [[nodiscard]] Result<void, DefaultErrorType> receive(std::vector<char>& data);
    [[nodiscard]] Result<void, DefaultErrorType> send(std::span<const char> data);
    Result<void, DefaultErrorType> disconnect();
    ~SocketConnection() { disconnect(); };

    // HTTP/2 support
    bool isHttp2() const { return m_isHttp2; }
    void setHttp2(bool isHttp2) { m_isHttp2 = isHttp2; }
    
    Result<std::string, DefaultErrorType> ip() const;
    Result<uint32_t, DefaultErrorType> port() const;
    constexpr SOCKET handle() const { return m_handle; };

    std::chrono::milliseconds keepAliveTimeout() const;
    std::chrono::milliseconds requestTimeout() const;
    std::chrono::milliseconds writeTimeout() const;
    std::chrono::seconds timeout() const;
    std::size_t maxRequest() const;

    constexpr std::size_t nbRequest() const {
        return m_nbRequest;
    };

    std::vector<char>& pendingData() { return m_pendingData; }
    const std::vector<char>& pendingData() const { return m_pendingData; }
    bool hasPendingData() const { return !m_pendingData.empty(); }
    void markRequestStarted();
    void clearRequestStarted();
    bool requestTimedOut(std::chrono::steady_clock::time_point now) const;
    void markIdle();
    bool keepAliveTimedOut(std::chrono::steady_clock::time_point now) const;

    // Set protocol preference for ALPN
    void setProtocolPreference(const std::vector<std::string>& protocols) {
        m_protocolPreference = protocols;
    }
    
    const std::vector<std::string>& getProtocolPreference() const {
        return m_protocolPreference;
    }
    
    // Check if client supports HTTP/2
    bool supportsProtocol(const std::string& protocol) const {
        for (const auto& p : m_protocolPreference) {
            if (p == protocol) return true;
        }
        return false;
    }

    constexpr bool isClosed() const
    {
        return m_handle == INVALID_SOCKET;
    }

    constexpr bool closeRequested() const
    {
        return m_closeRequested;
    }

    constexpr void requestClose()
    {
        m_closeRequested = true;
    }

    void onClose(std::function<void()> callback)
    {
        m_onClose = callback;
    }

#ifdef _WIN32
    SocketConnection(SOCKET handle, SOCKADDR_IN clientData);
#else
    SocketConnection(SOCKET handle, struct sockaddr_in clientData);
#endif

private:
#ifdef _WIN32
    SOCKADDR_IN m_clientData;
#else
    struct sockaddr_in m_clientData;
#endif
    SOCKET m_handle;
    std::size_t m_nbRequest;
    bool m_closeRequested = false;
    bool m_isHttp2 = false;
    std::function<void()> m_onClose;
    std::vector<std::string> m_protocolPreference;
    std::array<char, 16384> m_recvBuffer = {};
    std::vector<char> m_pendingData;
    std::chrono::steady_clock::time_point m_requestStartedAt{};
    std::chrono::steady_clock::time_point m_idleSince{};
    bool m_hasRequestStarted = false;
};

#pragma once
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <Result.hpp>
#include <array>
#include <chrono>
#include <span>
#include <thread>
#include <utility>
#include <vector>

class NetworkSocket;

class SocketConnection
{
    friend NetworkSocket;

public:
    SocketConnection(const SocketConnection&) = delete;
    SocketConnection& operator=(const SocketConnection&) = delete;
    SocketConnection(SocketConnection&& other) noexcept
        : m_handle(std::exchange(other.m_handle, INVALID_SOCKET)),
        m_nbRequest(std::exchange(other.m_nbRequest, 0)),
        m_clientData(std::exchange(other.m_clientData, SOCKADDR_IN{})) {};
    SocketConnection& operator=(SocketConnection&& other) noexcept
    {
        std::swap(m_handle, other.m_handle);
        std::swap(m_nbRequest, other.m_nbRequest);
        std::swap(m_clientData, other.m_clientData);

        return *this;
    };

    [[nodiscard]] Result<void, DefaultErrorType> receive(std::vector<char>& data);
    [[nodiscard]] Result<void, DefaultErrorType> send(std::span<const char> data);
    [[nodiscard]] Result<void, DefaultErrorType> disconnect();
    ~SocketConnection() { disconnect(); };

    [[nodiscard]] Result<std::string, DefaultErrorType> ip() const;
    [[nodiscard]] Result<uint32_t, DefaultErrorType> port() const;
    [[nodiscard]] constexpr SOCKET handle() const { return m_handle; };

    constexpr auto timeout() const {
        using namespace std::chrono_literals;
        return 70s;
    };

    constexpr std::size_t maxRequest() const {
        return 1000;
    };

    constexpr std::size_t nbRequest() const {
        return m_nbRequest;
    };

    [[nodiscard]] constexpr bool isClosed() const
    {
        return m_handle == INVALID_SOCKET;
    }

    [[nodiscard]] constexpr bool closeRequested() const
    {
        return m_closeRequested;
    }

    constexpr void requestClose()
    {
        m_closeRequested = true;
    }

private:
    SocketConnection(SOCKET handle, SOCKADDR_IN clientData);

private:
    SOCKADDR_IN m_clientData{};
    SOCKET m_handle{INVALID_SOCKET};
    bool m_closeRequested{false};
    std::array<char, 512> m_recvBuffer{};
    std::size_t m_nbRequest{0};
};

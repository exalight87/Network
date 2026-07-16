#pragma once

#include <test_curl/kernel/HttpRequest.hpp>
#include <test_curl/kernel/HttpResponse.hpp>
#include <test_curl/kernel/HttpRoute.hpp>
#include <test_curl/kernel/HttpServer.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

inline uint16_t nextTestPort()
{
    static std::atomic<uint16_t> nextPort{12000};
    return nextPort.fetch_add(1);
}

inline void addDefaultTestRoutes(HttpServer& server)
{
    server.addRoute({
        .route = "ping",
        .callable = [](const HttpRequest&, HttpResponse& response) -> bool
        {
            response.body = "pong";
            response.code = 200;
            return true;
        },
        .description = "Health check endpoint"
    });

    server.addRoute({
        .route = "your-post-endpoint",
        .allowedMethods = {HttpRequest::POST},
        .callable = [](const HttpRequest&, HttpResponse& response) -> bool
        {
            response.body = "POST received";
            response.code = 200;
            return true;
        },
        .description = "POST endpoint used by integration tests"
    });

    server.addRoute({
        .route = "",
        .callable = [](const HttpRequest&, HttpResponse& response) -> bool
        {
            response.body = "ok";
            response.code = 200;
            return true;
        }
    });
}

class TestServer
{
public:
    explicit TestServer(uint16_t port)
        : m_port(port)
    {
        m_server.port(m_port);
        addDefaultTestRoutes(m_server);

        auto result = m_server.startAsync();
        if (!result)
        {
            throw std::runtime_error(result.GetError().GetFormatedError());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    ~TestServer()
    {
        m_server.stop();
    }

    std::string baseUrl() const
    {
        return "http://127.0.0.1:" + std::to_string(m_port);
    }

    uint16_t port() const
    {
        return m_port;
    }

private:
    uint16_t m_port;
    HttpServer m_server;
};

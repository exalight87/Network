#include "HttpServer.hpp"
#include <iostream>
#include <HttpRoute.hpp>
#include <chrono>
#include <SocketConnection.hpp>
#include <ConnectionPool.hpp>
#include <ScopeGuard.hpp>
#include <Http2.hpp>
#include <cstring>  // For memcmp

namespace
{
    Result<HttpRequest, HttpServerError> _getHttpRequest(std::shared_ptr<SocketConnection> connection, const std::vector<char>* initialData);
    bool _ShouldKeepAlive(const HttpRequest& request);
    bool _IsHttp2Connection(const std::vector<char>& data);
    void _HandleHttp2Connection(std::shared_ptr<SocketConnection> connection, const std::vector<char>& initialData);
}

Result<void, HttpServerError> HttpServer::start()
{
    m_pool = std::make_unique<ConnectionPool>(
        [this](std::stop_token stopToken, std::shared_ptr<SocketConnection> connection)
        {
            ScopeGuard guard([&connection] {
                if (!connection->isClosed())
                {
                    connection->disconnect();
                }
                });

            // Check if this is an HTTP/2 connection
            std::vector<char> initialData;
            auto rData = connection->receive(initialData);
            
            if (rData && !initialData.empty())
            {
                if (_IsHttp2Connection(initialData))
                {
                    std::cout << "HTTP/2 connection detected\n";
                    connection->setHttp2(true);
                    _HandleHttp2Connection(connection, initialData);
                    return;
                }
            }

            while (!stopToken.stop_requested() && !connection->isClosed() && !connection->closeRequested())
            {
                std::cout << "Connection : " << connection->handle() << '\n';

                HttpRequest request;
                HttpResponse response;
                bool routeFound = false;
                Result<HttpRequest, HttpServerError> rRequest;

                try
                {
                    rRequest = _getHttpRequest(connection, &initialData);
                    initialData.clear();  // Clear after first use

                    if (rRequest)
                    {
                        request = std::move(rRequest).Data();

                        if (request.headers["Connection"] == "close")
                        {
                            std::cout << "  Connection closed received\n";
                            auto result = connection->send(HttpResponse::CLOSE_CONNECTION.format());
                            if (!result)
                            {
                                std::cerr << result.GetError().GetFormatedError() << "\n";
                            }
                            break;
                        }

                        auto rCLientIp = connection->ip();
                        if (!rCLientIp)
                        {
                            std::cerr << "  Can't get ip from client connection\n";
                            // break;
                        }
                        std::cout << std::format("  Request from [ {} ] on : {}\n", rCLientIp.DataOr("Unknown").c_str(), request.url.path);


                        auto cleanedPath = request.url.path.substr(1);
                        auto splitedPath = std::views::split(cleanedPath, '/');
                        for (auto& route : m_routes)
                        {
                            if ((routeFound = route(splitedPath, request, response)))
                            {
                                break;
                            }
                        }
                    }
                }
                catch (...)
                {
                    std::exception_ptr eptr = std::current_exception();
                    try
                    {
                        if (eptr)
                            std::rethrow_exception(eptr);
                    }
                    catch (const std::exception& e)
                    {
                        std::cout << "Caught exception: '" << e.what() << "'\nreturn 404 error\n";
                    }
                }

                if (!rRequest && rRequest.GetError().type == HttpServerError::CloseRequested)
                {
                    connection->requestClose();
                    response = HttpResponse::CLOSE_CONNECTION;
                }
                else if (!routeFound)
                {
                    response = HttpResponse::CODE_404;
                }

                // Headers added by the server
                auto rIp = ip();
                auto rPort = port();
                if (rIp && rPort)
                {
                    response.headers["Host"] = std::format("{}:{}", rIp.Data().c_str(), rPort.Data());
                }
                response.headers["Handle"] = std::to_string(connection->handle());

                if (_ShouldKeepAlive(request)) {
                    response.headers["Connection"] = "keep-alive";
                    response.headers["Keep-Alive"] = std::format("timeout={}, max={}", connection->timeout().count(), connection->maxRequest());
                } else {
                    response.headers["Connection"] = "close";
                }

                auto result = connection->send(response.format());

                if (!result)
                {
                    std::cerr << result.GetError().GetFormatedError() << "\n";
                }

                // Check max requests for aggressive reuse
                if (connection->nbRequest() >= connection->maxRequest()) {
                    break;
                }
            }
        }
    );

    auto rSocketStart = NetworkSocket::start();
    if (!rSocketStart)
    {
        return Error(HttpServerError::NotSpecialized, "Fail to start socket : " + rSocketStart.GetError().GetFormatedError());
    }

    return {};
}

void HttpServer::addRoute(HttpRoute&& route)
{
    m_routes.push_back(std::forward< HttpRoute >(route));
}


namespace {
    Result<HttpRequest, HttpServerError> _getHttpRequest(std::shared_ptr<SocketConnection> connection, const std::vector<char>* initialData = nullptr)
    {
        using namespace std::chrono_literals;
        std::vector<char> data;
        
        // Use initial data if provided
        if (initialData && !initialData->empty()) {
            data = *initialData;
        }
        
        auto begin = std::chrono::high_resolution_clock::now();
        while (data.empty())
        {
            if ((std::chrono::high_resolution_clock::now() - begin) > connection->timeout())
            {
                return Error(HttpServerError::CloseRequested, std::format("Connection {} waiting since more than {}s\n", connection->handle(), connection->timeout().count()));
            }

            if (connection->nbRequest() >= connection->maxRequest())
            {
                return Error(HttpServerError::CloseRequested, std::format("Connection {} reach max number of requests of {}\n", connection->handle(), connection->maxRequest()));
            }

            auto result = connection->receive(data);
            if (!result)
            {
                return Error(HttpServerError::NotSpecialized, result.GetError().GetFormatedError());
            }
        }

        HttpRequest request;
        if (!request.parse(data.data()))
        {
            return Error(HttpServerError::NotSpecialized, "Fail to parse http request");
        }
        return request;
    }

    bool _ShouldKeepAlive(const HttpRequest& request)
    {
        std::string httpVersion = request.httpVersion;

        auto headerIt = request.headers.find("Connection");
        
        if (httpVersion == "HTTP/1.1") {
            if (headerIt == request.headers.end()) {
                return true;
            }
            return headerIt->second != "close";
        }
        else if (httpVersion == "HTTP/1.0") {
            if (headerIt == request.headers.end()) {
                return false;
            }
            return headerIt->second == "keep-alive";
        }

        return false;
    }

    [[maybe_unused]] bool _IsHttp2Request(const std::vector<char>& data)
    {
        // HTTP/2 connection preface: "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
        if (data.size() >= 24) {
            const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
            return std::memcmp(data.data(), preface, 24) == 0;
        }
        return false;
    }

    bool _IsHttp2Connection(const std::vector<char>& data)
    {
        // HTTP/2 connection preface: "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
        if (data.size() >= 24) {
            const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
            return std::memcmp(data.data(), preface, 24) == 0;
        }
        return false;
    }

    void _HandleHttp2Connection(std::shared_ptr<SocketConnection> connection, const std::vector<char>& initialData)
    {
        Http2Connection http2Conn;
        std::vector<char> buffer;
        
        // Process the initial data
        std::vector<uint8_t> response;
        auto result = http2Conn.processData(
            reinterpret_cast<const uint8_t*>(initialData.data()), 
            initialData.size()
        );
        
        if (result) {
            response = result.Data();
            if (!response.empty()) {
                auto sendResult = connection->send(std::span<const char>(
                    reinterpret_cast<const char*>(response.data()), 
                    response.size()
                ));
                if (!sendResult) {
                    std::cerr << "HTTP/2 send error: " << sendResult.GetError().GetFormatedError() << "\n";
                }
            }
        }
        
        // Continue handling HTTP/2 frames
        while (!connection->isClosed() && !connection->closeRequested())
        {
            buffer.clear();
            auto rData = connection->receive(buffer);
            
            if (!rData || buffer.empty()) {
                if (!rData) {
                    std::cerr << "HTTP/2 receive error: " << rData.GetError().GetFormatedError() << "\n";
                }
                break;
            }
            
            // Process incoming frames
            result = http2Conn.processData(
                reinterpret_cast<const uint8_t*>(buffer.data()), 
                buffer.size()
            );
            
            if (result) {
                response = result.Data();
                if (!response.empty()) {
                    auto sendResult = connection->send(std::span<const char>(
                        reinterpret_cast<const char*>(response.data()), 
                        response.size()
                    ));
                    if (!sendResult) {
                        std::cerr << "HTTP/2 send error: " << sendResult.GetError().GetFormatedError() << "\n";
                    }
                }
            } else {
                std::cerr << "HTTP/2 processing error: " << result.GetError().GetFormatedError() << "\n";
                break;
            }
        }
        
        std::cout << "HTTP/2 connection closed\n";
    }

    [[maybe_unused]] Result<std::string, HttpServerError> _getHttp2Request(std::shared_ptr<SocketConnection> connection)
    {
        std::vector<char> data;
        auto rData = connection->receive(data);
        
        if (!rData)
        {
            return Error(HttpServerError::NotSpecialized, rData.GetError().GetFormatedError());
        }

        if (data.empty())
        {
            return Error(HttpServerError::NotSpecialized, "Empty HTTP/2 request");
        }

        // Simple HTTP/2 response for now
        return std::string(data.begin(), data.end());
    }
}
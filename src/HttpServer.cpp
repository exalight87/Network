#include "HttpServer.hpp"
#include <iostream>
#include <HttpRoute.hpp>
#include <chrono>
#include <SocketConnection.hpp>
#include <ConnectionPool.hpp>
#include <ScopeGuard.hpp>

namespace
{
    Result<HttpRequest, HttpServerError> _getHttpRequest(std::shared_ptr<SocketConnection> connection);
    bool _ShouldKeepAlive(const HttpRequest& request);
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

            while (!stopToken.stop_requested() && !connection->isClosed() && !connection->closeRequested())
            {
                std::cout << "Connection : " << connection->handle() << '\n';

                HttpRequest request;
                HttpResponse response;
                bool routeFound = false;
                Result<HttpRequest, HttpServerError> rRequest;

                try
                {
                    rRequest = _getHttpRequest(connection);

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
                }

                auto result = connection->send(response.format());

                if (!result)
                {
                    std::cerr << result.GetError().GetFormatedError() << "\n";
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
    Result<HttpRequest, HttpServerError> _getHttpRequest(std::shared_ptr<SocketConnection> connection)
    {
        using namespace std::chrono_literals;
        std::vector<char> data;
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
}
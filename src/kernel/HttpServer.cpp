#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>
#include <string_view>
#include <test_curl/kernel/ConnectionPool.hpp>
#include <test_curl/kernel/Http2.hpp>
#include <test_curl/kernel/HttpRoute.hpp>
#include <test_curl/kernel/HttpServer.hpp>
#include <test_curl/kernel/ScopeGuard.hpp>
#include <test_curl/kernel/SocketConnection.hpp>

namespace
{
Result<HttpRequest, HttpServerError> _getHttpRequest(std::stop_token stopToken,
                                                     std::shared_ptr<SocketConnection> connection,
                                                     const std::vector<char>* initialData = nullptr);
bool _ShouldKeepAlive(const HttpRequest& request);
bool _IsHttp2Connection(const std::vector<char>& data);
void _HandleHttp2Connection(std::shared_ptr<SocketConnection> connection, const std::vector<char>& initialData);
std::optional<std::size_t> _ExpectedRequestSize(std::span<const char> data);
bool _IEquals(std::string_view lhs, std::string_view rhs);
std::string_view _Trim(std::string_view value);
std::optional<std::string_view> _HeaderValue(const HttpRequest& request, std::string_view name);
} // namespace

Result<void, HttpServerError> HttpServer::start()
{
    if (m_autoDocsEnabled && !m_autoDocsRouteRegistered)
    {
        auto rPort = port();
        uint16_t portNum = rPort ? rPort.Data() : 8080;

        auto docsRoute = HttpRoute{};
        docsRoute.route = "docs";
        docsRoute.allowedMethods = {HttpRequest::GET};
        docsRoute.description = "API Documentation";
        docsRoute.callable = [this, portNum](const HttpRequest& request, HttpResponse& response) -> bool
        {
            response.body = ApiDocs::generateHtml(m_routes, portNum);
            response.headers["Content-Type"] = "text/html; charset=utf-8";
            response.code = 200;
            return true;
        };
        if (m_routeRegistry.addRoute(docsRoute.route, HttpRequest::GET, docsRoute.route))
        {
            m_routes.push_back(std::move(docsRoute));
            m_autoDocsRouteRegistered = true;
        }
    }

    m_pool = std::make_unique<ConnectionPool>(
        [this](std::stop_token stopToken, std::shared_ptr<SocketConnection> connection)
        {
            ScopeGuard guard(
                [&connection]
                {
                    if (!connection->isClosed())
                    {
                        connection->disconnect();
                    }
                });

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
                HttpRequest request;
                HttpResponse response;
                auto routeResult = HttpRoute::MatchResult::NoMatch;
                Result<HttpRequest, HttpServerError> rRequest;

                try
                {
                    rRequest = _getHttpRequest(stopToken, connection, &initialData);
                    initialData.clear();

                    if (rRequest)
                    {
                        request = std::move(rRequest).Data();

                        if (auto connectionHeader = _HeaderValue(request, "Connection");
                            connectionHeader && _IEquals(*connectionHeader, "close"))
                        {
                            std::cout << "  Connection closed received\n";
                            auto result = connection->send(
                                HttpResponse::CLOSE_CONNECTION.format(request.method != HttpRequest::HEAD));
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
                        }
                        std::cout << std::format("  Request from [ {} ] on : {}\n", rCLientIp.DataOr("Unknown").c_str(),
                                                 request.url.path);

                        auto cleanedPath = request.url.path.substr(1);
                        auto splitedPath = std::views::split(cleanedPath, '/');
                        for (auto& route : m_routes)
                        {
                            routeResult = route(splitedPath, request, response);
                            if (routeResult != HttpRoute::MatchResult::NoMatch)
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
                    break;
                }
                else if (!rRequest &&
                         rRequest.GetError().GetFormatedError().find("Connection closed") != std::string::npos)
                {
                    if (!connection->isClosed())
                    {
                        response.code = 400;
                        response.body = "Bad Request: Connection closed by client";
                        auto result = connection->send(response.format(request.method != HttpRequest::HEAD));
                        if (!result)
                        {
                            std::cerr << result.GetError().GetFormatedError() << "\n";
                        }
                    }
                    break;
                }
                else if (!rRequest)
                {
                    response.code = 400;
                    response.body = "Bad Request: " + rRequest.GetError().GetFormatedError();
                }
                else if (routeResult == HttpRoute::MatchResult::NoMatch)
                {
                    response = HttpResponse::CODE_404;
                }

                auto rIp = ip();
                auto rPort = port();
                if (rIp && rPort)
                {
                    response.headers["Host"] = std::format("{}:{}", rIp.Data().c_str(), rPort.Data());
                }
                response.headers["Handle"] = std::to_string(connection->handle());

                const bool keepAlive =
                    _ShouldKeepAlive(request) && !stopToken.stop_requested() && !connection->closeRequested();

                if (keepAlive)
                {
                    response.headers["Connection"] = "keep-alive";
                    response.headers["Keep-Alive"] =
                        std::format("timeout={}, max={}", connection->timeout().count(), connection->maxRequest());
                }
                else
                {
                    response.headers["Connection"] = "close";
                }

                auto result = connection->send(response.format(request.method != HttpRequest::HEAD));

                if (!result)
                {
                    std::cerr << result.GetError().GetFormatedError() << "\n";
                }

                if (!keepAlive)
                {
                    connection->requestClose();
                    break;
                }

                if (connection->nbRequest() >= connection->maxRequest())
                {
                    connection->requestClose();
                    break;
                }
            }
        });

    auto rSocketStart = NetworkSocket::start();
    if (!rSocketStart)
    {
        return Error(HttpServerError::NotSpecialized,
                     "Fail to start socket : " + rSocketStart.GetError().GetFormatedError());
    }

    return {};
}

Result<void, DefaultErrorType> HttpServer::startAsync()
{
    if (m_serverThread.joinable())
    {
        return Error(DefaultErrorType::AlreadyRunning, "HTTP server thread is already running");
    }

    m_serverThread = std::jthread(
        [this]
        {
            auto result = start();
            if (!result)
            {
                std::cerr << result.GetError().GetFormatedError() << '\n';
            }
        });

    return {};
}

void HttpServer::addRoute(HttpRoute&& route)
{
    bool success = true;

    std::function<void(HttpRoute&, std::string_view)> registerRoute = [&](HttpRoute& r, std::string_view parentPath)
    {
        std::string fullPath;
        bool isEmptyRoute = r.route.empty();

        if (parentPath.empty())
        {
            fullPath = r.route;
        }
        else if (isEmptyRoute)
        {
            fullPath = parentPath;
        }
        else
        {
            fullPath = std::string(parentPath) + "/" + std::string(r.route);
        }

        bool hasHandler = r.callable.has_value() || !r.allowedMethods.empty();

        if (hasHandler || r.subRoutes.empty())
        {
            if (!r.allowedMethods.empty())
            {
                for (auto method : r.allowedMethods)
                {
                    if (!m_routeRegistry.addRoute(r.route, method, fullPath))
                    {
                        std::cerr << "[WARNING] Duplicate route not added: " << fullPath << "\n";
                        success = false;
                        return;
                    }
                }
            }
            else
            {
                m_routeRegistry.addRoute(r.route, HttpRequest::UNKNOWN, fullPath);
            }
        }

        for (auto& subRoute : r.subRoutes)
        {
            registerRoute(subRoute, fullPath);
        }
    };

    registerRoute(route, {});

    if (success)
    {
        m_routes.push_back(std::forward<HttpRoute>(route));
    }
}

void HttpServer::clearRoutes()
{
    m_routes.clear();
    m_routeRegistry.clear();
    m_autoDocsRouteRegistered = false;
}

std::size_t HttpServer::routeCount() const
{
    return m_routes.size();
}

void HttpServer::enableAutoDocs()
{
    m_autoDocsEnabled = true;
}

namespace
{
Result<HttpRequest, HttpServerError> _getHttpRequest(std::stop_token stopToken,
                                                     std::shared_ptr<SocketConnection> connection,
                                                     const std::vector<char>* initialData)
{
    using namespace std::chrono_literals;

    std::vector<char> data;

    if (initialData && !initialData->empty())
    {
        data = *initialData;
    }

    auto begin = std::chrono::steady_clock::now();

    while (true)
    {
        if (stopToken.stop_requested() || connection->isClosed() || connection->closeRequested())
        {
            return Error(HttpServerError::CloseRequested, "Stop requested");
        }

        if ((std::chrono::steady_clock::now() - begin) > connection->timeout())
        {
            return Error(HttpServerError::CloseRequested,
                         std::format("Connection {} waiting since more than {}s\n", connection->handle(),
                                     connection->timeout().count()));
        }

        if (connection->nbRequest() >= connection->maxRequest())
        {
            return Error(HttpServerError::CloseRequested,
                         std::format("Connection {} reach max number of requests of {}\n", connection->handle(),
                                     connection->maxRequest()));
        }

        if (auto expectedSize = _ExpectedRequestSize(data); expectedSize && data.size() >= *expectedSize)
        {
            break;
        }

        const auto beforeReceiveSize = data.size();

        auto result = connection->receive(data);
        if (!result)
        {
            return Error(HttpServerError::CloseRequested, result.GetError().GetFormatedError());
        }

        if (data.size() == beforeReceiveSize)
        {
            std::this_thread::sleep_for(1ms);
            continue;
        }
    }

    HttpRequest request;
    if (!request.parse(std::string_view(data.data(), data.size())))
    {
        return Error(HttpServerError::NotSpecialized, "Fail to parse http request");
    }

    return request;
}

bool _ShouldKeepAlive(const HttpRequest& request)
{
    std::string httpVersion = request.httpVersion;

    auto connectionHeader = _HeaderValue(request, "Connection");

    if (httpVersion == "HTTP/1.1")
    {
        if (!connectionHeader)
        {
            return true;
        }
        return !_IEquals(*connectionHeader, "close");
    }
    else if (httpVersion == "HTTP/1.0")
    {
        if (!connectionHeader)
        {
            return false;
        }
        return _IEquals(*connectionHeader, "keep-alive");
    }

    return false;
}

[[maybe_unused]] bool _IsHttp2Request(const std::vector<char>& data)
{
    if (data.size() >= 24)
    {
        const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        return std::memcmp(data.data(), preface, 24) == 0;
    }
    return false;
}

bool _IsHttp2Connection(const std::vector<char>& data)
{
    if (data.size() >= 24)
    {
        const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        return std::memcmp(data.data(), preface, 24) == 0;
    }
    return false;
}

void _HandleHttp2Connection(std::shared_ptr<SocketConnection> connection, const std::vector<char>& initialData)
{
    Http2Connection http2Conn;
    std::vector<char> buffer;

    std::vector<uint8_t> response;
    auto result = http2Conn.processData(reinterpret_cast<const uint8_t*>(initialData.data()), initialData.size());

    if (result)
    {
        response = result.Data();
        if (!response.empty())
        {
            auto sendResult = connection->send(
                std::span<const char>(reinterpret_cast<const char*>(response.data()), response.size()));
            if (!sendResult)
            {
                std::cerr << "HTTP/2 send error: " << sendResult.GetError().GetFormatedError() << "\n";
            }
        }
    }

    while (!connection->isClosed() && !connection->closeRequested())
    {
        buffer.clear();
        auto rData = connection->receive(buffer);

        if (!rData || buffer.empty())
        {
            if (!rData)
            {
                std::cerr << "HTTP/2 receive error: " << rData.GetError().GetFormatedError() << "\n";
            }
            break;
        }

        result = http2Conn.processData(reinterpret_cast<const uint8_t*>(buffer.data()), buffer.size());

        if (result)
        {
            response = result.Data();
            if (!response.empty())
            {
                auto sendResult = connection->send(
                    std::span<const char>(reinterpret_cast<const char*>(response.data()), response.size()));
                if (!sendResult)
                {
                    std::cerr << "HTTP/2 send error: " << sendResult.GetError().GetFormatedError() << "\n";
                }
            }
        }
        else
        {
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

    return std::string(data.begin(), data.end());
}

std::optional<std::size_t> _ExpectedRequestSize(std::span<const char> data)
{
    if (data.empty())
    {
        return std::nullopt;
    }

    const std::string_view request(data.data(), data.size());
    const auto headersEnd = request.find("\r\n\r\n");
    if (headersEnd == std::string_view::npos)
    {
        return std::nullopt;
    }

    std::size_t contentLength = 0;
    std::size_t lineStart = request.find("\r\n");
    if (lineStart == std::string_view::npos)
    {
        return std::nullopt;
    }
    lineStart += 2;

    while (lineStart < headersEnd)
    {
        const auto lineEnd = request.find("\r\n", lineStart);
        if (lineEnd == std::string_view::npos || lineEnd > headersEnd)
        {
            return std::nullopt;
        }

        const auto line = request.substr(lineStart, lineEnd - lineStart);
        const auto separator = line.find(':');
        if (separator != std::string_view::npos && _IEquals(_Trim(line.substr(0, separator)), "Content-Length"))
        {
            const auto value = _Trim(line.substr(separator + 1));
            const auto begin = value.data();
            const auto end = value.data() + value.size();
            if (std::from_chars(begin, end, contentLength).ec != std::errc{})
            {
                return std::nullopt;
            }
        }

        lineStart = lineEnd + 2;
    }

    return headersEnd + 4 + contentLength;
}

bool _IEquals(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i])))
        {
            return false;
        }
    }
    return true;
}

std::string_view _Trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::optional<std::string_view> _HeaderValue(const HttpRequest& request, std::string_view name)
{
    for (const auto& [key, value] : request.headers)
    {
        if (_IEquals(key, name))
        {
            return value;
        }
    }
    return std::nullopt;
}
} // namespace

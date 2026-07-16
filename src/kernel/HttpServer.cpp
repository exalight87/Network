#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
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
enum class RequestReadStatus
{
    Complete,
    WouldBlock,
    BadRequest,
    PayloadTooLarge,
    HeadersTooLarge,
    UriTooLong,
    NotImplemented,
    HttpVersionNotSupported
};

struct RequestSizeProbe
{
    RequestReadStatus status = RequestReadStatus::WouldBlock;
    std::size_t size = 0;
    std::string message;
};

Result<HttpRequest, HttpServerError> _getHttpRequest(std::stop_token stopToken,
                                                     std::shared_ptr<SocketConnection> connection);
bool _ShouldKeepAlive(const HttpRequest& request);
bool _IsHttp2Connection(const std::vector<char>& data);
void _HandleHttp2Connection(std::shared_ptr<SocketConnection> connection, const std::vector<char>& initialData);
RequestSizeProbe _ExpectedRequestSize(std::span<const char> data);
HttpServerError _ToServerError(RequestReadStatus status);
uint32_t _StatusCodeForError(HttpServerError error);
std::size_t _EnvSize(const char* name, std::size_t defaultValue);
bool _IEquals(std::string_view lhs, std::string_view rhs);
std::string_view _Trim(std::string_view value);
std::optional<std::string_view> _HeaderValue(const HttpRequest& request, std::string_view name);
bool _ParseRequestLine(std::string_view requestLine, std::string_view& method, std::string_view& target,
                       std::string_view& version);
bool _IsKnownMethod(std::string_view method);
bool _IsValidToken(std::string_view value);
bool _ValidateHeaderName(std::string_view name);
bool _ValidateHeaderValue(std::string_view value);
bool _ParseContentLength(std::string_view value, std::size_t& contentLength);
bool _TraceConnectionLifecycle();
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
        [this](std::stop_token stopToken, std::shared_ptr<SocketConnection> connection) -> ConnectionPool::Action
        {
            if (stopToken.stop_requested() || connection->isClosed() || connection->closeRequested())
            {
                return ConnectionPool::Action::Close;
            }

            HttpRequest request;
            HttpResponse response;
            auto routeResult = HttpRoute::MatchResult::NoMatch;
            Result<HttpRequest, HttpServerError> rRequest;

            try
            {
                if (_IsHttp2Connection(connection->pendingData()))
                {
                    if (_TraceConnectionLifecycle())
                    {
                        std::cout << "HTTP/2 connection detected\n";
                    }
                    connection->setHttp2(true);
                    _HandleHttp2Connection(connection, connection->pendingData());
                    return ConnectionPool::Action::Close;
                }

                rRequest = _getHttpRequest(stopToken, connection);

                if (rRequest)
                {
                    request = std::move(rRequest).Data();

                    auto rCLientIp = connection->ip();
                    if (!rCLientIp)
                    {
                        std::cerr << "  Can't get ip from client connection\n";
                    }
                    if (_TraceConnectionLifecycle())
                    {
                        std::cout << std::format("  Request from [ {} ] on : {}\n",
                                                 rCLientIp.DataOr("Unknown").c_str(), request.url.path);
                    }

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
                    rRequest = Error(HttpServerError::NotSpecialized, e.what());
                }
            }

            if (!rRequest && rRequest.GetError().type == HttpServerError::WouldBlock)
            {
                return ConnectionPool::Action::WaitForRead;
            }
            else if (!rRequest && rRequest.GetError().type == HttpServerError::CloseRequested)
            {
                connection->requestClose();
                return ConnectionPool::Action::Close;
            }
            else if (!rRequest &&
                     rRequest.GetError().GetFormatedError().find("Connection closed") != std::string::npos)
            {
                if (!connection->isClosed())
                {
                    response.code = 400;
                    response.body = "Bad Request: Connection closed by client";
                    response.headers["Connection"] = "close";
                    auto result = connection->send(response.format(request.method != HttpRequest::HEAD));
                    if (!result)
                    {
                        std::cerr << result.GetError().GetFormatedError() << "\n";
                    }
                }
                return ConnectionPool::Action::Close;
            }
            else if (!rRequest)
            {
                response.code = _StatusCodeForError(rRequest.GetError().type);
                response.body = rRequest.GetError().message.empty() ? "Bad Request" : rRequest.GetError().message;
                response.headers["Connection"] = "close";
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

            const bool willReachMaxRequests = connection->nbRequest() + 1 >= connection->maxRequest();
            const bool keepAlive = rRequest && _ShouldKeepAlive(request) && !willReachMaxRequests &&
                                   !stopToken.stop_requested() && !connection->closeRequested();

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
                return ConnectionPool::Action::Close;
            }

            if (_TraceConnectionLifecycle())
            {
                std::cout << std::format("  Response sent on {}, decision={}\n", connection->handle(),
                                         keepAlive ? "keep-alive" : "close");
            }

            if (!keepAlive)
            {
                connection->requestClose();
                return ConnectionPool::Action::Close;
            }

            return connection->hasPendingData() ? ConnectionPool::Action::Requeue : ConnectionPool::Action::WaitForRead;
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
                                                     std::shared_ptr<SocketConnection> connection)
{
    auto& data = connection->pendingData();

    while (true)
    {
        if (stopToken.stop_requested() || connection->isClosed() || connection->closeRequested())
        {
            return Error(HttpServerError::CloseRequested, "Stop requested");
        }

        const auto now = std::chrono::steady_clock::now();
        if (connection->requestTimedOut(now))
        {
            return Error(HttpServerError::CloseRequested,
                         std::format("Connection {} waiting for a complete request since more than {}ms\n",
                                     connection->handle(), connection->requestTimeout().count()));
        }

        if (connection->nbRequest() >= connection->maxRequest())
        {
            return Error(HttpServerError::CloseRequested,
                         std::format("Connection {} reach max number of requests of {}\n", connection->handle(),
                                     connection->maxRequest()));
        }

        if (const auto probe = _ExpectedRequestSize(data); probe.status == RequestReadStatus::Complete &&
                                                      data.size() >= probe.size)
        {
            break;
        }
        else if (probe.status != RequestReadStatus::WouldBlock && probe.status != RequestReadStatus::Complete)
        {
            return Error(_ToServerError(probe.status), probe.message);
        }

        const auto beforeReceiveSize = data.size();

        auto result = connection->receive(data);
        if (!result)
        {
            return Error(HttpServerError::CloseRequested, result.GetError().GetFormatedError());
        }

        if (data.size() == beforeReceiveSize)
        {
            return Error(HttpServerError::WouldBlock, "No complete request available yet");
        }

        if (!data.empty())
        {
            connection->markRequestStarted();
        }
    }

    const auto expectedSize = _ExpectedRequestSize(data);
    if (expectedSize.status != RequestReadStatus::Complete)
    {
        if (expectedSize.status != RequestReadStatus::WouldBlock)
        {
            return Error(_ToServerError(expectedSize.status), expectedSize.message);
        }
        return Error(HttpServerError::WouldBlock, "No complete request available yet");
    }

    HttpRequest request;
    if (!request.parse(std::string_view(data.data(), expectedSize.size)))
    {
        return Error(HttpServerError::NotSpecialized, "Fail to parse http request");
    }

    data.erase(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(expectedSize.size));
    connection->clearRequestStarted();

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

bool _TraceConnectionLifecycle()
{
    const char* value = std::getenv("HTTP_SERVER_TRACE_CONNECTIONS");
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
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

RequestSizeProbe _ExpectedRequestSize(std::span<const char> data)
{
    const std::size_t maxHeaderBytes = _EnvSize("HTTP_SERVER_MAX_HEADER_BYTES", 16 * 1024);
    const std::size_t maxHeaderCount = _EnvSize("HTTP_SERVER_MAX_HEADER_COUNT", 200);
    const std::size_t maxBodyBytes = _EnvSize("HTTP_SERVER_MAX_BODY_SIZE_BYTES", 8 * 1024 * 1024);
    const std::size_t maxTargetBytes = _EnvSize("HTTP_SERVER_MAX_TARGET_BYTES", 8 * 1024);

    if (data.empty())
    {
        return {.status = RequestReadStatus::WouldBlock};
    }

    const std::string_view request(data.data(), data.size());
    const auto requestLineEnd = request.find("\r\n");
    if (requestLineEnd == std::string_view::npos)
    {
        if (request.size() > maxHeaderBytes)
        {
            return {.status = RequestReadStatus::HeadersTooLarge, .message = "Request headers too large"};
        }
        return {.status = RequestReadStatus::WouldBlock};
    }

    std::string_view method;
    std::string_view target;
    std::string_view version;
    if (!_ParseRequestLine(request.substr(0, requestLineEnd), method, target, version))
    {
        return {.status = RequestReadStatus::BadRequest, .message = "Malformed request line"};
    }
    if (!_IsValidToken(method))
    {
        return {.status = RequestReadStatus::BadRequest, .message = "Invalid HTTP method"};
    }
    if (!_IsKnownMethod(method))
    {
        return {.status = RequestReadStatus::NotImplemented, .message = "HTTP method not implemented"};
    }
    if (target.size() > maxTargetBytes)
    {
        return {.status = RequestReadStatus::UriTooLong, .message = "Request target too long"};
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0")
    {
        return {.status = RequestReadStatus::HttpVersionNotSupported, .message = "HTTP version not supported"};
    }

    const auto headersEnd = request.find("\r\n\r\n");
    if (headersEnd == std::string_view::npos)
    {
        if (request.size() > maxHeaderBytes)
        {
            return {.status = RequestReadStatus::HeadersTooLarge, .message = "Request headers too large"};
        }
        return {.status = RequestReadStatus::WouldBlock};
    }
    if (headersEnd + 4 > maxHeaderBytes)
    {
        return {.status = RequestReadStatus::HeadersTooLarge, .message = "Request headers too large"};
    }

    std::size_t contentLength = 0;
    bool hasContentLength = false;
    bool hasTransferEncoding = false;
    bool hasHost = false;
    std::size_t headerCount = 0;
    std::size_t lineStart = requestLineEnd + 2;

    while (lineStart < headersEnd)
    {
        const auto lineEnd = request.find("\r\n", lineStart);
        if (lineEnd == std::string_view::npos || lineEnd > headersEnd)
        {
            return {.status = RequestReadStatus::WouldBlock};
        }

        const auto line = request.substr(lineStart, lineEnd - lineStart);
        ++headerCount;
        if (headerCount > maxHeaderCount)
        {
            return {.status = RequestReadStatus::HeadersTooLarge, .message = "Too many request headers"};
        }

        const auto separator = line.find(':');
        if (separator == std::string_view::npos || separator == 0)
        {
            return {.status = RequestReadStatus::BadRequest, .message = "Malformed header"};
        }
        if (separator > 0 && (line[separator - 1] == ' ' || line[separator - 1] == '\t'))
        {
            return {.status = RequestReadStatus::BadRequest, .message = "Malformed header"};
        }

        const auto name = line.substr(0, separator);
        const auto rawValue = line.substr(separator + 1);
        const auto value = _Trim(rawValue);
        if (!_ValidateHeaderName(name) || !_ValidateHeaderValue(rawValue))
        {
            return {.status = RequestReadStatus::BadRequest, .message = "Malformed header"};
        }

        if (_IEquals(name, "Content-Length"))
        {
            if (hasContentLength)
            {
                return {.status = RequestReadStatus::BadRequest, .message = "Duplicate Content-Length"};
            }
            if (!_ParseContentLength(value, contentLength))
            {
                return {.status = RequestReadStatus::BadRequest, .message = "Invalid Content-Length"};
            }
            hasContentLength = true;
        }
        else if (_IEquals(name, "Transfer-Encoding"))
        {
            hasTransferEncoding = true;
        }
        else if (_IEquals(name, "Host"))
        {
            hasHost = !value.empty();
        }

        lineStart = lineEnd + 2;
    }

    if (version == "HTTP/1.1" && !hasHost)
    {
        return {.status = RequestReadStatus::BadRequest, .message = "Missing Host header"};
    }
    if (hasTransferEncoding && hasContentLength)
    {
        return {.status = RequestReadStatus::BadRequest, .message = "Transfer-Encoding cannot be combined with Content-Length"};
    }
    if (hasTransferEncoding)
    {
        return {.status = RequestReadStatus::NotImplemented, .message = "Transfer-Encoding is not supported"};
    }
    if (contentLength > maxBodyBytes)
    {
        return {.status = RequestReadStatus::PayloadTooLarge, .message = "Request body too large"};
    }

    return {.status = RequestReadStatus::Complete, .size = headersEnd + 4 + contentLength};
}

HttpServerError _ToServerError(RequestReadStatus status)
{
    switch (status)
    {
    case RequestReadStatus::BadRequest:
        return HttpServerError::BadRequest;
    case RequestReadStatus::PayloadTooLarge:
        return HttpServerError::PayloadTooLarge;
    case RequestReadStatus::HeadersTooLarge:
        return HttpServerError::HeadersTooLarge;
    case RequestReadStatus::UriTooLong:
        return HttpServerError::UriTooLong;
    case RequestReadStatus::NotImplemented:
        return HttpServerError::NotImplemented;
    case RequestReadStatus::HttpVersionNotSupported:
        return HttpServerError::HttpVersionNotSupported;
    case RequestReadStatus::Complete:
    case RequestReadStatus::WouldBlock:
        break;
    }
    return HttpServerError::BadRequest;
}

uint32_t _StatusCodeForError(HttpServerError error)
{
    switch (error)
    {
    case HttpServerError::PayloadTooLarge:
        return 413;
    case HttpServerError::UriTooLong:
        return 414;
    case HttpServerError::HeadersTooLarge:
        return 431;
    case HttpServerError::NotImplemented:
        return 501;
    case HttpServerError::HttpVersionNotSupported:
        return 505;
    case HttpServerError::BadRequest:
    case HttpServerError::NotSpecialized:
    case HttpServerError::CloseRequested:
    case HttpServerError::WouldBlock:
        return 400;
    }
    return 400;
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

bool _ParseRequestLine(std::string_view requestLine, std::string_view& method, std::string_view& target,
                       std::string_view& version)
{
    if (requestLine.empty())
    {
        return false;
    }

    for (const unsigned char c : requestLine)
    {
        if (c <= 31 || c == 127)
        {
            return false;
        }
    }

    const auto firstSpace = requestLine.find(' ');
    if (firstSpace == std::string_view::npos || firstSpace == 0)
    {
        return false;
    }

    const auto secondSpace = requestLine.find(' ', firstSpace + 1);
    if (secondSpace == std::string_view::npos || secondSpace == firstSpace + 1)
    {
        return false;
    }

    if (requestLine.find(' ', secondSpace + 1) != std::string_view::npos || secondSpace + 1 == requestLine.size())
    {
        return false;
    }

    method = requestLine.substr(0, firstSpace);
    target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    version = requestLine.substr(secondSpace + 1);
    return true;
}

bool _IsKnownMethod(std::string_view method)
{
    return _IEquals(method, "GET") || _IEquals(method, "POST") || _IEquals(method, "PUT") ||
           _IEquals(method, "DELETE") || _IEquals(method, "PATCH") || _IEquals(method, "HEAD") ||
           _IEquals(method, "OPTIONS");
}

bool _IsValidToken(std::string_view value)
{
    if (value.empty())
    {
        return false;
    }

    for (const unsigned char c : value)
    {
        const bool isTokenChar = std::isalnum(c) || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
                                 c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' ||
                                 c == '_' || c == '`' || c == '|' || c == '~';
        if (!isTokenChar)
        {
            return false;
        }
    }

    return true;
}

bool _ValidateHeaderName(std::string_view name)
{
    return _IsValidToken(name);
}

bool _ValidateHeaderValue(std::string_view value)
{
    for (const unsigned char c : value)
    {
        if ((c < 32 && c != '\t') || c == 127)
        {
            return false;
        }
    }
    return true;
}

bool _ParseContentLength(std::string_view value, std::size_t& contentLength)
{
    if (value.empty())
    {
        return false;
    }

    for (const unsigned char c : value)
    {
        if (!std::isdigit(c))
        {
            return false;
        }
    }

    const auto begin = value.data();
    const auto end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, contentLength);
    return ec == std::errc{} && ptr == end;
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

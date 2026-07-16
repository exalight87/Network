#pragma once

#include <test_curl/kernel/NetworkSocket.hpp>
#include <memory>
#include <vector>
#include <test_curl/kernel/Result.hpp>
#include <test_curl/kernel/RouteRegistry.hpp>
#include <test_curl/kernel/ApiDocs.hpp>

class HttpServer : public NetworkSocket
{
public:
    [[nodiscard]] Result<void, HttpServerError> start();
    [[nodiscard]] Result<void, DefaultErrorType> startAsync();

    void addRoute(HttpRoute&& route);

    void clearRoutes();

    std::size_t routeCount() const;

    void enableAutoDocs();

private:
    std::vector<HttpRoute> m_routes;
    RouteRegistry m_routeRegistry;
    bool m_autoDocsEnabled = false;
    bool m_autoDocsRouteRegistered = false;
};

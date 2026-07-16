#pragma once

#include <NetworkSocket.hpp>
#include <memory>
#include <vector>
#include <Result.hpp>
#include <RouteRegistry.hpp>

class HttpServer : public NetworkSocket
{
public:
    [[nodiscard]] Result<void, HttpServerError> start();

    void addRoute(HttpRoute&& route);

    void clearRoutes();

    std::size_t routeCount() const;

private:
    std::vector<HttpRoute> m_routes;
};

#pragma once

#include <NetworkSocket.hpp>
#include <memory>
#include <vector>
#include <Result.hpp>

class HttpServer : public NetworkSocket
{
public:
    [[nodiscard]] Result<void, HttpServerError> start();

    void addRoute(HttpRoute&& route);

private:
    std::vector<HttpRoute> m_routes;
};
#include <test_curl/kernel/HttpServer.hpp>
#include <test_curl/kernel/HttpRoute.hpp>
#include <test_curl/kernel/HttpRequest.hpp>
#include <test_curl/kernel/HttpResponse.hpp>
#include <iostream>

int main(int argc, char **argv)
{
    // Configure server port (default: 8080)
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    // Create HTTP server instance
    HttpServer server;
    server.port(port);

    // Add a simple route that returns "Hello, World!"
    server.addRoute({
        .route = "",
        .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
        {
            response.body = "Hello, World!";
            response.code = 200;
            return true;
        }
    });

    // Start the server
    std::cout << "Starting simple server on port " << port << "...\n" << std::flush;
    if (auto rServer = server.start(); !rServer)
    {
        std::cerr << rServer.GetError().GetFormatedError();
        return 1;
    }

    return 0;
}

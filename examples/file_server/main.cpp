#include <HttpServer.hpp>
#include <HttpRoute.hpp>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <iostream>
#include <filesystem>

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

    // Create a public directory for static files
    std::filesystem::create_directories("public");

    // Add route to serve static files from "public/" directory
    server.addRoute({
        .route = "",
        .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
        {
            // Build file path inside the configured public root.
            std::string filePath = request.url.path;
            
            // Default to index.html for root path
            if (request.url.path == "/" || request.url.path.empty()) {
                filePath = "index.html";
            }

            // Try to load the file
            if (response.loadFileFrom("public", filePath)) {
                response.code = 200;
                return true;
            }

            // File not found
            response.body = "File not found: " + request.url.path;
            response.code = 404;
            return true;
        }
    });

    // Start the server
    std::cout << "Starting file server on port " << port << "...\n";
    std::cout << "Serving files from: public/\n";
    if (auto rServer = server.start(); !rServer)
    {
        std::cerr << rServer.GetError().GetFormatedError();
        return 1;
    }

    return 0;
}

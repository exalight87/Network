#include <HttpServer.hpp>
#include <HttpRoute.hpp>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <HttpPage.hpp>
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

    // API endpoints with nested routes
    server.addRoute({
        .route = "api",
        .subRoutes = {
            {
                .route = "users",
                .subRoutes = {
                    {
                        .route = "{id}/info",
                        .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
                        {
                            auto it = request.pathParams.find("id");
                            if (it == request.pathParams.end()) {
                                response.body = R"({"error": "Missing id"})";
                                response.code = 400;
                                response.headers["Content-Type"] = "application/json";
                                return true;
                            }
                            
                            std::string idStr = it->second;
                            int id = 0;
                            try {
                                id = std::stoi(idStr);
                            } catch (...) {
                                response.body = R"({"error": "Invalid id"})";
                                response.code = 400;
                                response.headers["Content-Type"] = "application/json";
                                return true;
                            }
                            
                            if (id < 1 || id > 3) {
                                response.body = R"({"error": "User not found"})";
                                response.code = 404;
                                response.headers["Content-Type"] = "application/json";
                                return true;
                            }
                            
                            const char* names[] = {"Alice", "Bob", "Charlie"};
                            response.body = std::string("{\"id\": ") + std::to_string(id) + ", \"name\": \"" + names[id-1] + "\"}";
                            response.headers["Content-Type"] = "application/json";
                            response.code = 200;
                            return true;
                        }
                    },
                    {
                        .route = "",
                        .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
                        {
                            response.body = R"({
    "users": [
        {"id": 1, "name": "Alice"},
        {"id": 2, "name": "Bob"},
        {"id": 3, "name": "Charlie"}
    ]
})";
                            response.headers["Content-Type"] = "application/json";
                            response.code = 200;
                            return true;
                        }
                    }
                }
            },
            {
                .route = "health",
                .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
                {
                    response.body = R"({"status": "ok"})";
                    response.headers["Content-Type"] = "application/json";
                    response.code = 200;
                    return true;
                }
            }
        }
    });

    // Home page at root path "/"
    server.addRoute({
        .route = "",
        .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
        {
            HttpPage page;
            page.setTitle("API Server");
            page.setBody(R"(
                <h1>API Server</h1>
                <p>Welcome to the API server example!</p>
                <h2>Available Endpoints:</h2>
                <ul>
                    <li><code>GET /</code> - This home page</li>
                    <li><code>GET /api/users</code> - Get all users</li>
                    <li><code>GET /api/health</code> - Health check</li>
                </ul>
            )");
            response.body = page;
            response.code = 200;
            return true;
        }
    });

    // HTML page: About
    server.addRoute({
        .route = "about",
        .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
        {
            HttpPage page;
            page.setTitle("About");
            page.setBody(R"(
                <h1>About This Server</h1>
                <p>This is an example API server built with the HTTP server library.</p>
                <p>It demonstrates:</p>
                <ul>
                    <li>RESTful API endpoints</li>
                    <li>JSON responses</li>
                    <li>HTML page generation</li>
                    <li>Route parameters</li>
                </ul>
            )");
            response.body = page;
            response.code = 200;
            return true;
        }
    });

    // Start the server
    std::cout << "Starting API server on port " << port << "...\n";
    std::cout << "Available endpoints:\n";
    std::cout << "  GET /                    - HTML home page\n";
    std::cout << "  GET /about               - HTML about page\n";
    std::cout << "  GET /api/users           - JSON user list\n";
    std::cout << "  GET /api/users/{id}/info - JSON user details\n";
    std::cout << "  GET /api/health          - JSON health check\n";
    
    if (auto rServer = server.start(); !rServer)
    {
        std::cerr << rServer.GetError().GetFormatedError();
        return 1;
    }

    return 0;
}

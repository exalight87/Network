#include <iostream>
#include <test_curl/kernel/HttpPage.hpp>
#include <test_curl/kernel/HttpRequest.hpp>
#include <test_curl/kernel/HttpResponse.hpp>
#include <test_curl/kernel/HttpRoute.hpp>
#include <test_curl/kernel/HttpServer.hpp>

int main(int argc, char** argv)
{
    uint16_t port = 9090;
    if (argc > 1)
    {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    HttpServer server;
    server.port(port);
    server.enableAutoDocs();

    server.addRoute({.route = "ping",
                     .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                     {
                         response.body = "pong";
                         response.code = 200;
                         return true;
                     },
                     .description = "Health check endpoint - returns 'pong'"});

    server.addRoute({.route = "your-post-endpoint",
                     .allowedMethods = {HttpRequest::Methods::POST},
                     .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                     {
                         response.body = "POST received";
                         response.code = 200;
                         return true;
                     },
                     .description = "Example POST endpoint"});

    server.addRoute({.route = "your-post-endpoint",
                     .allowedMethods = {HttpRequest::Methods::POST},
                     .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                     {
                         response.body = "POST received";
                         response.code = 200;
                         return true;
                     },
                     .description = "Example POST endpoint"});

    server.addRoute(
        {.route = "api",
         .subRoutes = {{.route = "1",
                        .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                        {
                            HttpPage page;
                            page.setTitle("Api V1");
                            page.setBody("<h1>Welcome to index v1</h1>");
                            response.body = page;
                            response.code = 200;
                            return true;
                        },
                        .subRoutes = {{.route = "test",
                                       .allowedMethods = {HttpRequest::Methods::POST},
                                       .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                                       {
                                           HttpPage page;
                                           page.setTitle("Test api V1");
                                           page.setBody("<h1>Welcome to test post v1</h1>");
                                           response.body = page;
                                           response.code = 200;
                                           return true;
                                       }},
                                      {.route = "test",
                                       .allowedMethods = {HttpRequest::Methods::GET},
                                       .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                                       {
                                           HttpPage page;
                                           page.setTitle("Test api V1");
                                           page.setBody("<h1>Welcome to test get v1</h1>");
                                           response.body = page;
                                           response.code = 200;
                                           return true;
                                       }}}},
                       {.route = "2",
                        .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                        {
                            HttpPage page;
                            page.setTitle("Api V2");
                            page.setBody("<h1>Welcome to index v2</h1>");
                            response.body = page;
                            response.code = 200;
                            return true;
                        },
                        .subRoutes = {{.route = "test",
                                       .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                                       {
                                           HttpPage page;
                                           page.setTitle("Test api V2");
                                           page.setBody("<h1>Welcome to test v2</h1>");
                                           response.body = page;
                                           response.code = 200;
                                           return true;
                                       }}}}}});

    server.addRoute({.route = "",
                     .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                     {
                         std::cout << "  Load index.html\n";
                         if (!response.loadFile("website/index.html"))
                         {
                             return false;
                         };
                         response.code = 200;
                         return true;
                     }});

    server.addRoute({.route = "resources",
                     .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                     {
                         std::cout << "  Load " + request.url.path + '\n';
                         const std::string prefix = "/resources/";
                         const auto resourcePath = request.url.path.starts_with(prefix)
                                                       ? request.url.path.substr(prefix.size())
                                                       : request.url.path;
                         if (!response.loadFileFrom("resources", resourcePath))
                         {
                             return false;
                         };
                         response.code = 200;
                         return true;
                     }});

    server.addRoute({.route = "foo/bar/toto",
                     .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                     {
                         response.body = "Ok man";
                         response.code = 200;
                         return true;
                     }});

    if (auto rServer = server.start(); !rServer)
    {
        std::cout << rServer.GetError().GetFormatedError();
    }

    return 0;
}

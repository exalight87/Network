#include <iostream>
#include <NetworkCurl.hpp>
#include <format>
#include <HttpServer.hpp>
#include <HttpRoute.hpp>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <HttpPage.hpp>

int main(int argc, char **argv)
{
     //auto &curl = NetworkCurl::GetInstance();
     //curl.EnableDebug();
     //NetworkResponse response = curl.Get("http://example.com");
     //std::cout << std::format("{}\n", response);

    HttpServer server;
    server.port(9090);

    server.addRoute({
            .route = "api",
            .subRoutes = {
                {
                    .route = "1",
                    .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                    {
                        HttpPage page;
                        page.setTitle( "Api V1");
                        page.setBody("<h1>Welcome to index v1</h1>");
                        response.body = page;
                        response.code = 200;
                        return true;
                    },
                    .subRoutes = {
                        {
                            .route = "test",
                            .allowedMethods = {HttpRequest::Methods::POST},
                            .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                            {
                                HttpPage page;
                                page.setTitle("Test api V1");
                                page.setBody("<h1>Welcome to test post v1</h1>");
                                response.body = page;
                                response.code = 200;
                                return true;
                            }
                        },
                        {
                            .route = "test",
                            .allowedMethods = {HttpRequest::Methods::GET},
                            .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                            {
                                HttpPage page;
                                page.setTitle("Test api V1");
                                page.setBody("<h1>Welcome to test get v1</h1>");
                                response.body = page;
                                response.code = 200;
                                return true;
                            }
                        }
                    }
                },
                {
                    .route = "2",
                    .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                    {
                        HttpPage page;
                        page.setTitle("Api V2");
                        page.setBody("<h1>Welcome to index v2</h1>");
                        response.body = page;
                        response.code = 200;
                        return true;
                    },
                    .subRoutes = {
                        {
                            .route = "test",
                            .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                            {
                                HttpPage page;
                                page.setTitle("Test api V2");
                                page.setBody("<h1>Welcome to test v2</h1>");
                                response.body = page;
                                response.code = 200;
                                return true;
                            }
                        }
                    }
                }
            }
        });

    server.addRoute({
            .route = "",
            .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                {
                    std::cout << "  Load index.html\n";
                    if (!response.loadFile("website/index.html"))
                    {
                        return false;
                    };
                    response.code = 200;
                    return true;
                }
        });

    server.addRoute({
            .route = "resources",
            .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                {
                    std::cout << "  Load " + request.url.path + '\n';
                    if(!response.loadFile(request.url.path))
                    {
                        return false;
                    };
                    response.code = 200;
                    return true;
                }
        });

    server.addRoute({
            .route = "foo/bar/toto",
            .callable = [](const HttpRequest& request, HttpResponse& response) -> bool
                {
                    response.body = "Ok man";
                    response.code = 200;
                    return true;
                }
        });

    if (auto rServer = server.start(); !rServer)
    {
        std::cout << rServer.Error().GetFormatedError();
    }

    return 0;
}

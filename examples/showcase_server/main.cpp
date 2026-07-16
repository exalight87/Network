#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <test_curl/kernel/HttpRequest.hpp>
#include <test_curl/kernel/HttpResponse.hpp>
#include <test_curl/kernel/HttpRoute.hpp>
#include <test_curl/kernel/HttpServer.hpp>

namespace
{
std::string jsonEscape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (character >= 0x20)
                escaped += static_cast<char>(character);
        }
    }
    return escaped;
}

void json(HttpResponse& response, uint32_t code, std::string body)
{
    response.code = code;
    response.body = std::move(body);
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    response.headers["Cache-Control"] = "no-store";
}
} // namespace

int main(int argc, char** argv)
{
    uint16_t port = 9090;
    if (argc > 1)
        port = static_cast<uint16_t>(std::stoi(argv[1]));

    HttpServer server;
    server.port(port);
    server.enableAutoDocs();

    server.addRoute({.route = "ping",
                     .callable = [](const HttpRequest&, HttpResponse& response)
                     {
                         json(response, 200, R"({"status":"ok","message":"pong"})");
                         return true;
                     },
                     .description = "Health check returning a deterministic JSON response"});

    server.addRoute({.route = "users/{id}",
                     .callable = [](const HttpRequest& request, HttpResponse& response)
                     {
                         const auto id = request.pathParams.find("id");
                         if (id == request.pathParams.end() || id->second.empty() || id->second.size() > 32)
                         {
                             json(response, 400, R"({"error":"invalid user id"})");
                             return true;
                         }
                         const auto details = request.url.queryParams.find("details");
                         const bool includeDetails = details != request.url.queryParams.end() && details->second == "true";
                         std::string body = "{\"id\":\"" + jsonEscape(id->second) + "\",\"name\":\"Ada\"";
                         if (includeDetails)
                             body += R"(,"details":{"role":"engineer","active":true})";
                         body += "}";
                         json(response, 200, std::move(body));
                         return true;
                     },
                     .description = "Demonstrates path and query parameters"});

    server.addRoute({.route = "echo",
                     .allowedMethods = {HttpRequest::POST},
                     .callable = [](const HttpRequest& request, HttpResponse& response)
                     {
                         constexpr size_t maxBodySize = 4096;
                         if (request.body.size() > maxBodySize)
                         {
                             json(response, 413, R"({"error":"body exceeds 4096 bytes"})");
                             return true;
                         }
                         json(response, 200,
                              "{\"received\":\"" + jsonEscape(request.body) +
                                  "\",\"bytes\":" + std::to_string(request.body.size()) + "}");
                         return true;
                     },
                     .description = "Echoes a bounded request body as JSON"});

    server.addRoute({.route = "errors/{code}",
                     .callable = [](const HttpRequest& request, HttpResponse& response)
                     {
                         const auto value = request.pathParams.find("code");
                         const std::string code = value == request.pathParams.end() ? "" : value->second;
                         if (code == "400")
                             json(response, 400, R"({"error":"demonstration bad request"})");
                         else if (code == "404")
                             json(response, 404, R"({"error":"demonstration resource not found"})");
                         else if (code == "422")
                             json(response, 422, R"({"error":"demonstration validation failure"})");
                         else
                             json(response, 400, R"({"error":"allowed codes are 400, 404 and 422"})");
                         return true;
                     },
                     .description = "Returns one of the safe demonstration error responses"});

    server.addRoute({.route = "",
                     .callable = [](const HttpRequest&, HttpResponse& response)
                     {
                         json(response, 200, R"({"name":"test-curl interactive demo","docs":"/docs"})");
                         return true;
                     },
                     .description = "Demo API metadata"});

    std::cout << "Demo HTTP server listening on 127.0.0.1:" << port << '\n';
    if (auto result = server.start(); !result)
    {
        std::cerr << result.GetError().GetFormatedError();
        return 1;
    }
    return 0;
}

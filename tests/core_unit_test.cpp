#include <test_curl/kernel/HttpRequest.hpp>
#include <test_curl/kernel/HttpResponse.hpp>
#include <test_curl/kernel/HttpRoute.hpp>

#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            throw std::runtime_error(std::string(message));
        }
    }

    void testHttpRequestParsesCompleteBody()
    {
        const std::string raw =
            "POST /submit?name=Jane+Doe HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 18\r\n"
            "\r\n"
            "line1\r\nline2=value";

        HttpRequest request;
        expect(request.parse(std::string_view(raw.data(), raw.size())), "request should parse");
        expect(request.method == HttpRequest::POST, "method should be POST");
        expect(request.url.path == "/submit", "path should be /submit");
        expect(request.url.queryParams.at("name") == "Jane Doe", "query param should be decoded");
        expect(request.body == "line1\r\nline2=value", "body should keep CRLF content");
    }

    void testDynamicRouteOwnsPath()
    {
        HttpRoute route;
        {
            std::string dynamicRoute = "dynamic";
            route.route = dynamicRoute;
        }
        route.callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            response.body = "ok";
            return true;
        };

        std::string path = "dynamic";
        auto splitPath = std::views::split(path, '/');
        HttpRequest request;
        request.method = HttpRequest::GET;
        HttpResponse response;

        expect(route(splitPath, request, response) == HttpRoute::MatchResult::Matched, "dynamic route should match");
        expect(response.code == 200, "dynamic route should set status 200");
    }

    void testMethodNotAllowedIsVisible()
    {
        HttpRoute route;
        route.route = "post-only";
        route.allowedMethods = {HttpRequest::POST};
        route.callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        };

        std::string path = "post-only";
        auto splitPath = std::views::split(path, '/');
        HttpRequest request;
        request.method = HttpRequest::GET;
        HttpResponse response;

        expect(route(splitPath, request, response) == HttpRoute::MatchResult::MethodNotAllowed, "GET should be rejected");
        expect(response.code == 405, "method mismatch should set 405");
        expect(response.headers.at("Allow") == "POST", "Allow header should expose POST");
    }

    void testStaticFilesStayInsideBaseDir()
    {
        const auto previousPath = std::filesystem::current_path();
        const auto testRoot = std::filesystem::path("/tmp/test_curl_core_unit");
        std::filesystem::remove_all(testRoot);
        std::filesystem::create_directories(testRoot / "public");
        std::filesystem::current_path(testRoot);

        {
            std::ofstream index("public/index.html");
            index << "hello";
        }

        HttpResponse ok;
        expect(ok.loadFileFrom("public", "index.html"), "file inside public should load");

        HttpResponse traversal;
        expect(!traversal.loadFileFrom("public", "../README.md"), "path traversal should be rejected");

        std::filesystem::current_path(previousPath);
        std::filesystem::remove_all(testRoot);
    }
}

int main()
{
    testHttpRequestParsesCompleteBody();
    testDynamicRouteOwnsPath();
    testMethodNotAllowedIsVisible();
    testStaticFilesStayInsideBaseDir();
    return 0;
}

#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <HttpRoute.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>

namespace
{
    void testHttpRequestParsesCompleteBody()
    {
        const std::string raw =
            "POST /submit?name=Jane+Doe HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 18\r\n"
            "\r\n"
            "line1\r\nline2=value";

        HttpRequest request;
        assert(request.parse(std::string_view(raw.data(), raw.size())));
        assert(request.method == HttpRequest::POST);
        assert(request.url.path == "/submit");
        assert(request.url.queryParams.at("name") == "Jane Doe");
        assert(request.body == "line1\r\nline2=value");
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

        assert(route(splitPath, request, response) == HttpRoute::MatchResult::Matched);
        assert(response.code == 200);
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

        assert(route(splitPath, request, response) == HttpRoute::MatchResult::MethodNotAllowed);
        assert(response.code == 405);
        assert(response.headers.at("Allow") == "POST");
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
        assert(ok.loadFileFrom("public", "index.html"));

        HttpResponse traversal;
        assert(!traversal.loadFileFrom("public", "../README.md"));

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

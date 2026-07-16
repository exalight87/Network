#include <gtest/gtest.h>
#include "../src/HttpServer.hpp"
#include "../src/HttpRoute.hpp"
#include "../src/HttpRequest.hpp"
#include "../src/HttpResponse.hpp"

class RouteDuplicateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        server = std::make_unique<HttpServer>();
    }

    void TearDown() override
    {
        server->clearRoutes();
    }

    std::unique_ptr<HttpServer> server;
};

TEST_F(RouteDuplicateTest, AddUniqueRoutes_Success)
{
    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::GET },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::POST },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    server->addRoute({
        .route = "api/posts",
        .allowedMethods = { HttpRequest::GET },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    EXPECT_EQ(server->routeCount(), 3);
}

TEST_F(RouteDuplicateTest, AddDuplicateRoute_Fails)
{
    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::GET },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    EXPECT_EQ(server->routeCount(), 1);

    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::GET },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    EXPECT_EQ(server->routeCount(), 1);
}

TEST_F(RouteDuplicateTest, AddSamePathDifferentMethod_Success)
{
    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::GET },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::POST },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::PUT },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::DELETE },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    EXPECT_EQ(server->routeCount(), 4);
}

TEST_F(RouteDuplicateTest, ClearRoutes_AllowsReadding)
{
    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::GET },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    EXPECT_EQ(server->routeCount(), 1);

    server->clearRoutes();
    EXPECT_EQ(server->routeCount(), 0);

    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::GET },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    EXPECT_EQ(server->routeCount(), 1);
}

TEST_F(RouteDuplicateTest, MultipleMethodsInAllowedMethods_RegistersEach)
{
    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::GET, HttpRequest::POST, HttpRequest::PUT },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    EXPECT_EQ(server->routeCount(), 1);

    server->addRoute({
        .route = "api/users",
        .allowedMethods = { HttpRequest::GET },
        .callable = [](const HttpRequest&, HttpResponse& response) {
            response.code = 200;
            return true;
        }
    });

    EXPECT_EQ(server->routeCount(), 1);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

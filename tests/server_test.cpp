#include <gtest/gtest.h>
#include <test_curl/integrations/curl/NetworkCurl.hpp>
#include "support/TestServer.hpp"

#include <memory>

class ServerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        server = std::make_unique<TestServer>(nextTestPort());
    }

    void TearDown() override
    {
        server.reset();
    }

    std::string baseUrl() const
    {
        return server->baseUrl();
    }

    NetworkCurl& client = NetworkCurl::GetInstance();

    std::unique_ptr<TestServer> server;
};

TEST_F(ServerTest, PingPongTest)
{
    auto response = client.Get(baseUrl() + "/ping");

    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "pong");
}

TEST_F(ServerTest, NonExistentEndpoint)
{
    auto response = client.Get(baseUrl() + "/nonexistent");

    EXPECT_EQ(response.code, 404);
}

TEST_F(ServerTest, PostRequestTest)
{
    std::string payload = "{\"key\": \"value\"}";
    auto response = client.Post(baseUrl() + "/your-post-endpoint", payload);

    EXPECT_EQ(response.code, 200);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

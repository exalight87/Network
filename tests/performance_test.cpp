#include "support/TestServer.hpp"
#include <gtest/gtest.h>
#include <test_curl/integrations/curl/NetworkCurl.hpp>

#include <future>
#include <memory>
#include <vector>

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

    std::string getBaseUrl() const
    {
        return server->baseUrl();
    }

    std::unique_ptr<TestServer> server;
};

TEST_F(ServerTest, HTTP11Request)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "pong");
}

TEST_F(ServerTest, KeepAliveReuse)
{
    auto& client = NetworkCurl::GetInstance();

    for (int i = 0; i < 5; ++i)
    {
        auto response = client.Get(getBaseUrl() + "/ping");
        EXPECT_EQ(response.code, 200);
        EXPECT_EQ(response.body, "pong");
    }
}

TEST_F(ServerTest, ConcurrentWithThreadPool)
{
    const int numThreads = 20;
    std::vector<std::future<NetworkResponse>> futures;

    for (int i = 0; i < numThreads; ++i)
    {
        futures.push_back(
            std::async(std::launch::async, [&]() { return NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping"); }));
    }

    int successCount = 0;
    for (auto& future : futures)
    {
        auto response = future.get();
        if (response.code == 200 && response.body == "pong")
        {
            successCount++;
        }
    }

    EXPECT_EQ(successCount, numThreads);
}

TEST_F(ServerTest, HTTP2ProtocolDetection)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "pong");
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

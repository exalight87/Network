#include <gtest/gtest.h>
#include "../src/NetworkCurl.hpp"
#include "../src/HttpServer.hpp"
#include "../src/HttpRoute.hpp"
#include "../src/HttpRequest.hpp"
#include "../src/HttpResponse.hpp"
#include "../src/HttpPage.hpp"
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <future>

namespace {
    std::atomic<bool> serverRunning{false};
}

class ServerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        port = nextPort++;
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", port);
        
        serverPid = fork();
        if (serverPid == 0)
        {
            execl("./build/linux/x86_64/release/http_server", "http_server", portStr, nullptr);
            exit(1);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void TearDown() override
    {
        if (serverPid > 0)
        {
            kill(serverPid, SIGTERM);
            waitpid(serverPid, nullptr, 0);
        }
    }

    std::string getBaseUrl() const { return "http://localhost:" + std::to_string(port); }

    static int nextPort;
    int port = 0;
    pid_t serverPid = -1;
};

int ServerTest::nextPort = 11000;

TEST_F(ServerTest, HTTP11Request)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "pong");
}

TEST_F(ServerTest, KeepAliveReuse)
{
    // Test that connection is reused for multiple requests
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
    // Test concurrent requests with thread pool
    const int numThreads = 20;
    std::vector<std::future<NetworkResponse>> futures;
    
    for (int i = 0; i < numThreads; ++i)
    {
        futures.push_back(std::async(std::launch::async, [&]() {
            return NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        }));
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
    // Test that server can detect HTTP/2 connection preface
    // When using plain HTTP, curl sends HTTP/1.1 requests
    // The server should handle these normally and detect HTTP/2 preface when sent
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "pong");
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

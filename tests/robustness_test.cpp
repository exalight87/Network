#include <gtest/gtest.h>
#include "../src/NetworkCurl.hpp"
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <atomic>
#include <future>
#include <vector>
#include <sstream>

class RobustnessTest : public ::testing::Test
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

int RobustnessTest::nextPort = 10000;

TEST_F(RobustnessTest, ValidRequest)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "pong");
}

TEST_F(RobustnessTest, ConcurrentRequests_10Threads)
{
    const int numThreads = 10;
    std::vector<std::future<NetworkResponse>> futures;
    
    for (int i = 0; i < numThreads; ++i)
    {
        futures.push_back(std::async(std::launch::async, [&]() {
            return NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        }));
    }
    
    for (auto& future : futures)
    {
        auto response = future.get();
        EXPECT_EQ(response.code, 200);
        EXPECT_EQ(response.body, "pong");
    }
}

TEST_F(RobustnessTest, HighVolume_100Requests)
{
    const int numRequests = 100;
    int successCount = 0;
    
    for (int i = 0; i < numRequests; ++i)
    {
        auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        if (response.code == 200 && response.body == "pong")
        {
            successCount++;
        }
    }
    
    EXPECT_EQ(successCount, numRequests);
}

TEST_F(RobustnessTest, InvalidRoute_404)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/this-does-not-exist");
    EXPECT_EQ(response.code, 404);
}

TEST_F(RobustnessTest, LargePayload_Post)
{
    std::string largePayload(10000, 'A');
    auto response = NetworkCurl::GetInstance().Post(getBaseUrl() + "/your-post-endpoint", largePayload);
    EXPECT_EQ(response.code, 200);
}

TEST_F(RobustnessTest, EmptyBody_Post)
{
    auto response = NetworkCurl::GetInstance().Post(getBaseUrl() + "/your-post-endpoint", "");
    EXPECT_EQ(response.code, 200);
}

TEST_F(RobustnessTest, MultipleRapidConnections)
{
    const int numConnections = 20;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < numConnections; ++i)
    {
        threads.emplace_back([&]() {
            auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
            EXPECT_EQ(response.code, 200);
        });
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
}

TEST_F(RobustnessTest, EmptyPath_Root)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/");
    EXPECT_NE(response.code, 0);
}

TEST_F(RobustnessTest, TrailingSlash)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping/");
    EXPECT_NE(response.code, 0);
}

TEST_F(RobustnessTest, DoubleSlashes)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "//ping");
    EXPECT_NE(response.code, 0);
}

TEST_F(RobustnessTest, RepeatedGetRequests)
{
    const int numRequests = 50;
    for (int i = 0; i < numRequests; ++i)
    {
        auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        EXPECT_EQ(response.code, 200) << "Failed at request " << i;
    }
}

TEST_F(RobustnessTest, MixedGetAndPost)
{
    for (int i = 0; i < 10; ++i)
    {
        auto getResp = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        EXPECT_EQ(getResp.code, 200);
        
        auto postResp = NetworkCurl::GetInstance().Post(getBaseUrl() + "/your-post-endpoint", "test");
        EXPECT_EQ(postResp.code, 200);
    }
}

TEST_F(RobustnessTest, RequestWithoutClose)
{
    for (int i = 0; i < 5; ++i)
    {
        auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        EXPECT_EQ(response.code, 200);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

TEST_F(RobustnessTest, SpecialCharactersInUrl)
{
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping?param=value%20encoded");
    EXPECT_NE(response.code, 0);
}

TEST_F(RobustnessTest, LongUrlPath)
{
    std::string longPath(5000, 'a');
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/" + longPath);
    EXPECT_NE(response.code, 0);
}

TEST_F(RobustnessTest, ConcurrentDifferentEndpoints)
{
    std::vector<std::future<NetworkResponse>> futures;
    
    futures.push_back(std::async(std::launch::async, [&]() {
        return NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
    }));
    futures.push_back(std::async(std::launch::async, [&]() {
        return NetworkCurl::GetInstance().Get(getBaseUrl() + "/nonexistent");
    }));
    futures.push_back(std::async(std::launch::async, [&]() {
        return NetworkCurl::GetInstance().Post(getBaseUrl() + "/your-post-endpoint", "test");
    }));
    
    auto r1 = futures[0].get();
    auto r2 = futures[1].get();
    auto r3 = futures[2].get();
    
    EXPECT_EQ(r1.code, 200);
    EXPECT_EQ(r2.code, 404);
    EXPECT_EQ(r3.code, 200);
}

TEST_F(RobustnessTest, StressTest_1000Requests)
{
    const int numRequests = 1000;
    int successCount = 0;
    int failCount = 0;
    
    for (int i = 0; i < numRequests; ++i)
    {
        auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        if (response.code == 200 && response.body == "pong")
        {
            successCount++;
        }
        else
        {
            failCount++;
        }
        
        if (i % 100 == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    EXPECT_GT(successCount, numRequests * 0.95);
    std::cout << "Success: " << successCount << "/" << numRequests << ", Failed: " << failCount << std::endl;
}

TEST_F(RobustnessTest, SequentialThenConcurrent)
{
    for (int i = 0; i < 5; ++i)
    {
        auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        EXPECT_EQ(response.code, 200);
    }
    
    std::vector<std::future<NetworkResponse>> futures;
    for (int i = 0; i < 10; ++i)
    {
        futures.push_back(std::async(std::launch::async, [&]() {
            return NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
        }));
    }
    
    for (auto& future : futures)
    {
        auto response = future.get();
        EXPECT_EQ(response.code, 200);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

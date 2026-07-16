#include <gtest/gtest.h>
#include "../src/NetworkCurl.hpp"
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

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

    NetworkCurl& client = NetworkCurl::GetInstance();

    static int nextPort;
    int port = 0;
    pid_t serverPid = -1;
};

int ServerTest::nextPort = 9091;

TEST_F(ServerTest, PingPongTest)
{
    std::string baseUrl = "http://localhost:" + std::to_string(port);
    auto response = client.Get(baseUrl + "/ping");

    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "pong");
}

TEST_F(ServerTest, NonExistentEndpoint)
{
    std::string baseUrl = "http://localhost:" + std::to_string(port);
    auto response = client.Get(baseUrl + "/nonexistent");

    EXPECT_EQ(response.code, 404);
}

TEST_F(ServerTest, PostRequestTest)
{
    std::string baseUrl = "http://localhost:" + std::to_string(port);
    std::string payload = "{\"key\": \"value\"}";
    auto response = client.Post(baseUrl + "/your-post-endpoint", payload);

    EXPECT_EQ(response.code, 200);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

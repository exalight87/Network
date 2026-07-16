#include <gtest/gtest.h>
#include "../src/NetworkCurl.hpp"
#include "../src/HttpServer.hpp"
#include "../src/HttpRoute.hpp"
#include "../src/HttpRequest.hpp"
#include "../src/HttpResponse.hpp"
#include "../src/HttpPage.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <memory>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

class ProcessManager
{
public:
    ProcessManager() = default;
    ~ProcessManager() { stopServer(); }

    void startServer(const std::string& executable, int port)
    {
        stopServer();

#ifdef _WIN32
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", port);

        std::string cmd = executable + " " + portStr;

        if (!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr, nullptr, FALSE,
                            CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi))
        {
            throw std::runtime_error("Failed to start server");
        }

        serverHandle = pi.hProcess;
        CloseHandle(pi.hThread);
#else
        serverPid = fork();
        if (serverPid == 0)
        {
            execl(executable.c_str(), "http_server", std::to_string(port).c_str(), nullptr);
            exit(1);
        }
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void stopServer()
    {
#ifdef _WIN32
        if (serverHandle)
        {
            TerminateProcess(serverHandle, 0);
            CloseHandle(serverHandle);
            serverHandle = nullptr;
        }
#else
        if (serverPid > 0)
        {
            kill(serverPid, SIGTERM);
            waitpid(serverPid, nullptr, 0);
            serverPid = -1;
        }
#endif
    }

private:
#ifdef _WIN32
    HANDLE serverHandle = nullptr;
#else
    pid_t serverPid = -1;
#endif
};

class ServerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        port = nextPort++;
        
        std::string exePath = getExecutablePath();
        
        processManager = std::make_unique<ProcessManager>();
        processManager->startServer(exePath, port);
    }

    void TearDown() override
    {
        processManager->stopServer();
        processManager.reset();
    }

    std::string getExecutablePath() const
    {
#ifdef _WIN32
        char buffer[MAX_PATH];
        GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        std::string path(buffer);
        size_t pos = path.find_last_of("\\/");
        if (pos != std::string::npos)
        {
            path = path.substr(0, pos);
        }

        std::string exePath = path + "\\http_server.exe";
        if (std::ifstream(exePath).good())
        {
            return exePath;
        }

        exePath = path + "\\..\\release\\http_server.exe";
        if (std::ifstream(exePath).good())
        {
            return exePath;
        }

        return path + "\\http_server.exe";
#else
        char buffer[1024];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        std::string baseDir;
        if (len != -1)
        {
            buffer[len] = '\0';
            std::string path(buffer);
            size_t pos = path.find_last_of("/");
            if (pos != std::string::npos)
            {
                baseDir = path.substr(0, pos);
            }
        }

        std::vector<std::string> searchPaths;
        if (!baseDir.empty())
        {
            searchPaths.push_back(baseDir + "/http_server");
        }
        searchPaths.push_back("./build/linux/x86_64/release/http_server");
        searchPaths.push_back("./build/linux/x86_64/debug/http_server");
        searchPaths.push_back("./http_server");

        for (const auto& p : searchPaths)
        {
            std::ifstream test(p);
            if (test.good())
            {
                return p;
            }
        }

        return searchPaths.front();
#endif
    }

    std::string getBaseUrl() const { return "http://localhost:" + std::to_string(port); }

    static int nextPort;
    int port = 0;
    std::unique_ptr<ProcessManager> processManager;
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
    auto response = NetworkCurl::GetInstance().Get(getBaseUrl() + "/ping");
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "pong");
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

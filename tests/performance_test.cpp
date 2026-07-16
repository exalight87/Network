#define NOMINMAX
#include "support/TestServer.hpp"

#include <curl/curl.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

enum class ConnectionMode
{
    KeepAlive,
    NewConnectionPerRequest
};

void EnsureCurlGlobalInitialized()
{
    static std::once_flag once;

    std::call_once(once,
                   []
                   {
                       const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
                       if (code != CURLE_OK)
                       {
                           throw std::runtime_error("curl_global_init failed");
                       }
                   });

    // Pas de curl_global_cleanup volontairement ici.
    // Dans un binaire de test, ça évite les problèmes d'ordre de destruction
    // avec d'autres singletons curl éventuels.
}

int EnvInt(const char* name, int defaultValue, int minValue = 1)
{
    const char* value = std::getenv(name);
    if (!value || std::string(value).empty())
    {
        return defaultValue;
    }

    try
    {
        return std::max(minValue, std::stoi(value));
    }
    catch (...)
    {
        return defaultValue;
    }
}

double EnvDouble(const char* name, double defaultValue, double minValue = 0.0)
{
    const char* value = std::getenv(name);
    if (!value || std::string(value).empty())
    {
        return defaultValue;
    }

    try
    {
        return std::max(minValue, std::stod(value));
    }
    catch (...)
    {
        return defaultValue;
    }
}

std::vector<int> EnvConcurrencyLevels()
{
    const char* value = std::getenv("HTTP_PERF_CONCURRENCY_LEVELS");
    if (!value || std::string(value).empty())
    {
        return {1, 2, 4, 8, 16, 32};
    }

    std::vector<int> levels;
    std::stringstream stream(value);
    std::string item;

    while (std::getline(stream, item, ','))
    {
        try
        {
            const int level = std::stoi(item);
            if (level > 0)
            {
                levels.push_back(level);
            }
        }
        catch (...)
        {
        }
    }

    if (levels.empty())
    {
        return {1, 2, 4, 8, 16, 32};
    }

    return levels;
}

double ElapsedMs(Clock::time_point begin, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::string Truncate(const std::string& value, std::size_t maxSize = 128)
{
    if (value.size() <= maxSize)
    {
        return value;
    }

    return value.substr(0, maxSize) + "...";
}

struct RawCurlResponse
{
    CURLcode curlCode = CURLE_OK;
    long httpCode = 0;
    std::string body;
    std::string error;
};

class PerfCurlClient
{
  public:
    explicit PerfCurlClient(ConnectionMode connectionMode = ConnectionMode::KeepAlive)
        : m_connectionMode(connectionMode)
    {
        EnsureCurlGlobalInitialized();

        m_curl = curl_easy_init();
        if (m_curl == nullptr)
        {
            throw std::runtime_error("curl_easy_init failed");
        }

        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &PerfCurlClient::WriteBody);
        curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(m_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

        const long connectTimeoutMs = EnvInt("HTTP_PERF_CONNECT_TIMEOUT_MS", 2000);
        const long requestTimeoutMs = EnvInt("HTTP_PERF_REQUEST_TIMEOUT_MS", 5000);

        curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs);
        curl_easy_setopt(m_curl, CURLOPT_TIMEOUT_MS, requestTimeoutMs);
    }

    ~PerfCurlClient()
    {
        if (m_curl != nullptr)
        {
            curl_easy_cleanup(m_curl);
        }
    }

    PerfCurlClient(const PerfCurlClient&) = delete;
    PerfCurlClient& operator=(const PerfCurlClient&) = delete;

    RawCurlResponse Get(const std::string& url)
    {
        RawCurlResponse response;

        char errorBuffer[CURL_ERROR_SIZE] = {};
        response.body.clear();

        struct curl_slist* headers = nullptr;

        if (m_connectionMode == ConnectionMode::NewConnectionPerRequest)
        {
            headers = curl_slist_append(headers, "Connection: close");
        }

        curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(m_curl, CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headers);

        if (m_connectionMode == ConnectionMode::NewConnectionPerRequest)
        {
            curl_easy_setopt(m_curl, CURLOPT_FRESH_CONNECT, 1L);
            curl_easy_setopt(m_curl, CURLOPT_FORBID_REUSE, 1L);
        }
        else
        {
            curl_easy_setopt(m_curl, CURLOPT_FRESH_CONNECT, 0L);
            curl_easy_setopt(m_curl, CURLOPT_FORBID_REUSE, 0L);
        }

        response.curlCode = curl_easy_perform(m_curl);

        curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &response.httpCode);

        if (headers != nullptr)
        {
            curl_slist_free_all(headers);
            curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, nullptr);
        }

        if (response.curlCode != CURLE_OK)
        {
            if (errorBuffer[0] != '\0')
            {
                response.error = errorBuffer;
            }
            else
            {
                response.error = curl_easy_strerror(response.curlCode);
            }
        }

        return response;
    }

  private:
    static size_t WriteBody(void* data, size_t size, size_t nmemb, void* userData)
    {
        const std::size_t totalSize = size * nmemb;
        auto* body = static_cast<std::string*>(userData);
        body->append(static_cast<const char*>(data), totalSize);
        return totalSize;
    }

    CURL* m_curl = nullptr;
    ConnectionMode m_connectionMode = ConnectionMode::KeepAlive;
};

struct RequestMetric
{
    bool ok = false;
    long code = 0;
    double latencyMs = 0.0;
    std::string error;
};

RequestMetric PerformPing(PerfCurlClient& client, const std::string& baseUrl)
{
    const auto begin = Clock::now();

    try
    {
        const auto response = client.Get(baseUrl + "/ping");
        const auto end = Clock::now();

        RequestMetric metric;
        metric.code = response.httpCode;
        metric.latencyMs = ElapsedMs(begin, end);
        metric.ok = response.curlCode == CURLE_OK && response.httpCode == 200 && response.body == "pong";

        if (!metric.ok)
        {
            std::ostringstream stream;
            stream << "curlCode=" << static_cast<int>(response.curlCode) << ", httpCode=" << response.httpCode
                   << ", body=\"" << Truncate(response.body) << "\"";

            if (!response.error.empty())
            {
                stream << ", error=\"" << response.error << "\"";
            }

            metric.error = stream.str();
        }

        return metric;
    }
    catch (const std::exception& e)
    {
        const auto end = Clock::now();

        RequestMetric metric;
        metric.ok = false;
        metric.latencyMs = ElapsedMs(begin, end);
        metric.error = std::string("exception=") + e.what();
        return metric;
    }
    catch (...)
    {
        const auto end = Clock::now();

        RequestMetric metric;
        metric.ok = false;
        metric.latencyMs = ElapsedMs(begin, end);
        metric.error = "unknown exception";
        return metric;
    }
}

struct PerfSummary
{
    std::size_t count = 0;
    std::size_t success = 0;
    std::size_t failure = 0;

    double wallMs = 0.0;
    double rps = 0.0;

    double min = 0.0;
    double avg = 0.0;
    double p50 = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

struct PerfResult
{
    PerfSummary summary;
    std::vector<std::string> errors;
};

void AppendError(std::vector<std::string>& errors, const RequestMetric& metric)
{
    if (!metric.ok && errors.size() < 10)
    {
        errors.push_back(metric.error);
    }
}

std::string JoinErrors(const std::vector<std::string>& errors)
{
    std::ostringstream stream;

    for (const auto& error : errors)
    {
        stream << "\n  - " << error;
    }

    return stream.str();
}

PerfResult BuildPerfResult(std::vector<double> latenciesMs, std::size_t success, std::size_t failure,
                           std::vector<std::string> errors, double wallMs)
{
    PerfResult result;

    result.summary.count = latenciesMs.size();
    result.summary.success = success;
    result.summary.failure = failure;
    result.summary.wallMs = wallMs;

    if (wallMs > 0.0)
    {
        result.summary.rps = static_cast<double>(latenciesMs.size()) * 1000.0 / wallMs;
    }

    if (latenciesMs.empty())
    {
        result.errors = std::move(errors);
        return result;
    }

    std::sort(latenciesMs.begin(), latenciesMs.end());

    auto percentile = [&](double p)
    {
        const double pos = (p / 100.0) * static_cast<double>(latenciesMs.size() - 1);
        const auto lo = static_cast<std::size_t>(std::floor(pos));
        const auto hi = static_cast<std::size_t>(std::ceil(pos));

        if (lo == hi)
        {
            return latenciesMs[lo];
        }

        const double alpha = pos - static_cast<double>(lo);
        return latenciesMs[lo] * (1.0 - alpha) + latenciesMs[hi] * alpha;
    };

    result.summary.min = latenciesMs.front();
    result.summary.max = latenciesMs.back();
    result.summary.avg =
        std::accumulate(latenciesMs.begin(), latenciesMs.end(), 0.0) / static_cast<double>(latenciesMs.size());
    result.summary.p50 = percentile(50.0);
    result.summary.p90 = percentile(90.0);
    result.summary.p95 = percentile(95.0);
    result.summary.p99 = percentile(99.0);

    result.errors = std::move(errors);
    return result;
}

void PrintResult(const std::string& name, const PerfResult& result)
{
    const auto& s = result.summary;

    std::cout << std::fixed << std::setprecision(3) << "[perf] " << name << " | requests=" << s.count
              << " success=" << s.success << " failure=" << s.failure << " wall_ms=" << s.wallMs << " rps=" << s.rps
              << " min_ms=" << s.min << " avg_ms=" << s.avg << " p50_ms=" << s.p50 << " p90_ms=" << s.p90
              << " p95_ms=" << s.p95 << " p99_ms=" << s.p99 << " max_ms=" << s.max << std::endl;
}

void ExpectHealthyResult(const std::string& name, const PerfResult& result)
{
    SCOPED_TRACE(name);

    PrintResult(name, result);

    ASSERT_GT(result.summary.count, 0u);
    EXPECT_EQ(result.summary.failure, 0u) << JoinErrors(result.errors);
    EXPECT_EQ(result.summary.success, result.summary.count);

    const double maxP99Ms = EnvDouble("HTTP_PERF_MAX_P99_MS", 1000.0, 0.0);
    if (maxP99Ms > 0.0)
    {
        EXPECT_LE(result.summary.p99, maxP99Ms);
    }

    const double minRps = EnvDouble("HTTP_PERF_MIN_RPS", 0.0, 0.0);
    if (minRps > 0.0)
    {
        EXPECT_GE(result.summary.rps, minRps);
    }
}

PerfResult RunSequentialPing(const std::string& baseUrl, int requestCount, ConnectionMode connectionMode)
{
    PerfCurlClient client(connectionMode);

    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(requestCount));

    std::size_t success = 0;
    std::size_t failure = 0;
    std::vector<std::string> errors;

    const auto wallBegin = Clock::now();

    for (int i = 0; i < requestCount; ++i)
    {
        auto metric = PerformPing(client, baseUrl);

        latencies.push_back(metric.latencyMs);

        if (metric.ok)
        {
            ++success;
        }
        else
        {
            ++failure;
            AppendError(errors, metric);
        }
    }

    const auto wallEnd = Clock::now();

    return BuildPerfResult(std::move(latencies), success, failure, std::move(errors), ElapsedMs(wallBegin, wallEnd));
}

struct WorkerResult
{
    std::vector<double> latencies;
    std::size_t success = 0;
    std::size_t failure = 0;
    std::vector<std::string> errors;
};

WorkerResult RunWorkerFixedRequests(const std::string& baseUrl, int requestCount, ConnectionMode connectionMode)
{
    PerfCurlClient client(connectionMode);

    WorkerResult result;
    result.latencies.reserve(static_cast<std::size_t>(requestCount));

    for (int i = 0; i < requestCount; ++i)
    {
        auto metric = PerformPing(client, baseUrl);

        result.latencies.push_back(metric.latencyMs);

        if (metric.ok)
        {
            ++result.success;
        }
        else
        {
            ++result.failure;
            AppendError(result.errors, metric);
        }
    }

    return result;
}

PerfResult MergeWorkerResults(std::vector<WorkerResult> workerResults, double wallMs)
{
    std::vector<double> latencies;
    std::size_t success = 0;
    std::size_t failure = 0;
    std::vector<std::string> errors;

    for (auto& worker : workerResults)
    {
        success += worker.success;
        failure += worker.failure;

        latencies.insert(latencies.end(), worker.latencies.begin(), worker.latencies.end());

        for (const auto& error : worker.errors)
        {
            if (errors.size() < 10)
            {
                errors.push_back(error);
            }
        }
    }

    return BuildPerfResult(std::move(latencies), success, failure, std::move(errors), wallMs);
}

PerfResult RunConcurrentPing(const std::string& baseUrl, int threadCount, int requestsPerThread,
                             ConnectionMode connectionMode)
{
    std::promise<void> startPromise;
    std::shared_future<void> startFuture(startPromise.get_future());

    std::vector<std::future<WorkerResult>> futures;
    futures.reserve(static_cast<std::size_t>(threadCount));

    for (int i = 0; i < threadCount; ++i)
    {
        futures.push_back(std::async(std::launch::async,
                                     [baseUrl, startFuture, requestsPerThread, connectionMode]() mutable
                                     {
                                         // Important :
                                         // chaque worker a son propre CURL*.
                                         PerfCurlClient client(connectionMode);

                                         startFuture.wait();

                                         WorkerResult result;
                                         result.latencies.reserve(static_cast<std::size_t>(requestsPerThread));

                                         for (int request = 0; request < requestsPerThread; ++request)
                                         {
                                             auto metric = PerformPing(client, baseUrl);

                                             result.latencies.push_back(metric.latencyMs);

                                             if (metric.ok)
                                             {
                                                 ++result.success;
                                             }
                                             else
                                             {
                                                 ++result.failure;
                                                 AppendError(result.errors, metric);
                                             }
                                         }

                                         return result;
                                     }));
    }

    const auto wallBegin = Clock::now();
    startPromise.set_value();

    std::vector<WorkerResult> workers;
    workers.reserve(static_cast<std::size_t>(threadCount));

    for (auto& future : futures)
    {
        workers.push_back(future.get());
    }

    const auto wallEnd = Clock::now();

    return MergeWorkerResults(std::move(workers), ElapsedMs(wallBegin, wallEnd));
}

PerfResult RunSustainedPing(const std::string& baseUrl, int threadCount, int durationMs, ConnectionMode connectionMode)
{
    std::promise<void> startPromise;
    std::shared_future<void> startFuture(startPromise.get_future());

    std::vector<std::future<WorkerResult>> futures;
    futures.reserve(static_cast<std::size_t>(threadCount));

    for (int i = 0; i < threadCount; ++i)
    {
        futures.push_back(std::async(std::launch::async,
                                     [baseUrl, startFuture, durationMs, connectionMode]() mutable
                                     {
                                         PerfCurlClient client(connectionMode);

                                         startFuture.wait();

                                         WorkerResult result;
                                         const auto deadline = Clock::now() + std::chrono::milliseconds(durationMs);

                                         while (Clock::now() < deadline)
                                         {
                                             auto metric = PerformPing(client, baseUrl);

                                             result.latencies.push_back(metric.latencyMs);

                                             if (metric.ok)
                                             {
                                                 ++result.success;
                                             }
                                             else
                                             {
                                                 ++result.failure;
                                                 AppendError(result.errors, metric);
                                             }
                                         }

                                         return result;
                                     }));
    }

    const auto wallBegin = Clock::now();
    startPromise.set_value();

    std::vector<WorkerResult> workers;
    workers.reserve(static_cast<std::size_t>(threadCount));

    for (auto& future : futures)
    {
        workers.push_back(future.get());
    }

    const auto wallEnd = Clock::now();

    return MergeWorkerResults(std::move(workers), ElapsedMs(wallBegin, wallEnd));
}
} // namespace

class ServerPerformanceTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        server = std::make_unique<TestServer>(nextTestPort());

        const int warmupCount = EnvInt("HTTP_PERF_WARMUP_REQUESTS", 20, 0);
        PerfCurlClient warmupClient(ConnectionMode::KeepAlive);

        for (int i = 0; i < warmupCount; ++i)
        {
            auto metric = PerformPing(warmupClient, getBaseUrl());
            ASSERT_TRUE(metric.ok) << metric.error;
        }
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

TEST_F(ServerPerformanceTest, BaselineSequentialLatency)
{
    const int requestCount = EnvInt("HTTP_PERF_BASELINE_REQUESTS", 200);

    auto result = RunSequentialPing(getBaseUrl(), requestCount, ConnectionMode::KeepAlive);

    ExpectHealthyResult("baseline_sequential_keep_alive", result);
}

TEST_F(ServerPerformanceTest, KeepAliveSingleClientManyRequests)
{
    const int requestCount = EnvInt("HTTP_PERF_KEEPALIVE_REQUESTS", 1000);

    auto result = RunSequentialPing(getBaseUrl(), requestCount, ConnectionMode::KeepAlive);

    ExpectHealthyResult("keep_alive_single_client_many_requests", result);
}

TEST_F(ServerPerformanceTest, NewConnectionPerRequestSequential)
{
    if (EnvInt("HTTP_PERF_ENABLE_CONNECTION_CHURN", 0, 0) == 0)
    {
        GTEST_SKIP() << "Disabled by default because connection churn can expose slow close/keep-alive handling. "
                     << "Set HTTP_PERF_ENABLE_CONNECTION_CHURN=1 to enable it.";
    }

    const int requestCount = EnvInt("HTTP_PERF_NEW_CONNECTION_REQUESTS", 30);

    auto result = RunSequentialPing(getBaseUrl(), requestCount, ConnectionMode::NewConnectionPerRequest);

    ExpectHealthyResult("new_connection_per_request_sequential", result);
}

TEST_F(ServerPerformanceTest, ConcurrentKeepAliveLoadMatrix)
{
    const int requestsPerThread = EnvInt("HTTP_PERF_REQUESTS_PER_THREAD", 50);
    const auto levels = EnvConcurrencyLevels();

    for (int concurrency : levels)
    {
        auto result = RunConcurrentPing(getBaseUrl(), concurrency, requestsPerThread, ConnectionMode::KeepAlive);

        ExpectHealthyResult("concurrent_keep_alive_" + std::to_string(concurrency) + "_threads_" +
                                std::to_string(requestsPerThread) + "_requests_per_thread",
                            result);
    }
}

TEST_F(ServerPerformanceTest, BurstManyClientsAtSameTime)
{
    const int concurrency = EnvInt("HTTP_PERF_BURST_CONCURRENCY", 64);

    auto result = RunConcurrentPing(getBaseUrl(), concurrency, 1, ConnectionMode::KeepAlive);

    ExpectHealthyResult("burst_" + std::to_string(concurrency) + "_clients", result);
}

TEST_F(ServerPerformanceTest, ConcurrentConnectionChurn)
{
    if (EnvInt("HTTP_PERF_ENABLE_CONNECTION_CHURN", 0, 0) == 0)
    {
        GTEST_SKIP() << "Disabled by default because connection churn is intentionally aggressive. "
                     << "Set HTTP_PERF_ENABLE_CONNECTION_CHURN=1 to enable it.";
    }

    const int concurrency = EnvInt("HTTP_PERF_CHURN_CONCURRENCY", 8);
    const int requestsPerThread = EnvInt("HTTP_PERF_CHURN_REQUESTS_PER_THREAD", 5);

    auto result =
        RunConcurrentPing(getBaseUrl(), concurrency, requestsPerThread, ConnectionMode::NewConnectionPerRequest);

    ExpectHealthyResult("concurrent_connection_churn_" + std::to_string(concurrency) + "_threads_" +
                            std::to_string(requestsPerThread) + "_requests_per_thread",
                        result);
}

TEST_F(ServerPerformanceTest, SustainedKeepAliveLoad)
{
    const int concurrency = EnvInt("HTTP_PERF_SUSTAINED_CONCURRENCY", 8);
    const int durationMs = EnvInt("HTTP_PERF_SUSTAINED_DURATION_MS", 1000);

    auto result = RunSustainedPing(getBaseUrl(), concurrency, durationMs, ConnectionMode::KeepAlive);

    ExpectHealthyResult("sustained_keep_alive_" + std::to_string(concurrency) + "_threads_" +
                            std::to_string(durationMs) + "_ms",
                        result);

    EXPECT_GE(result.summary.success, static_cast<std::size_t>(concurrency));
}

TEST_F(ServerPerformanceTest, PerformanceDoesNotCollapseAfterManyKeepAliveRequests)
{
    const int requestCount = EnvInt("HTTP_PERF_LONG_RUN_REQUESTS", 2000);
    const int half = std::max(1, requestCount / 2);

    auto firstHalf = RunSequentialPing(getBaseUrl(), half, ConnectionMode::KeepAlive);

    auto secondHalf = RunSequentialPing(getBaseUrl(), requestCount - half, ConnectionMode::KeepAlive);

    ExpectHealthyResult("long_run_first_half", firstHalf);
    ExpectHealthyResult("long_run_second_half", secondHalf);

    const double degradationFactor = EnvDouble("HTTP_PERF_MAX_DEGRADATION_FACTOR", 5.0, 1.0);
    const double degradationSlackMs = EnvDouble("HTTP_PERF_MAX_DEGRADATION_SLACK_MS", 10.0, 0.0);

    EXPECT_LE(secondHalf.summary.p95, firstHalf.summary.p95 * degradationFactor + degradationSlackMs);
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#define NOMINMAX
#include "support/TestServer.hpp"

#include <curl/curl.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;

#ifndef _WIN32
using RawSocket = int;
constexpr RawSocket InvalidRawSocket = -1;
#else
using RawSocket = SOCKET;
constexpr RawSocket InvalidRawSocket = INVALID_SOCKET;
#endif

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

bool EnvFlag(const char* name, bool defaultValue = false)
{
    const char* value = std::getenv(name);
    if (!value || std::string(value).empty())
    {
        return defaultValue;
    }

    return std::string_view(value) != "0";
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
    struct CurlTimings
    {
        double nameLookupMs = 0.0;
        double connectMs = 0.0;
        double appConnectMs = 0.0;
        double preTransferMs = 0.0;
        double startTransferMs = 0.0;
        double totalMs = 0.0;
    } timings;
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
        curl_easy_setopt(m_curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

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
        response.timings = Timings();

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
    RawCurlResponse::CurlTimings Timings() const
    {
        auto getMs = [this](CURLINFO info)
        {
            double seconds = 0.0;
            curl_easy_getinfo(m_curl, info, &seconds);
            return seconds * 1000.0;
        };

        return {
            .nameLookupMs = getMs(CURLINFO_NAMELOOKUP_TIME),
            .connectMs = getMs(CURLINFO_CONNECT_TIME),
            .appConnectMs = getMs(CURLINFO_APPCONNECT_TIME),
            .preTransferMs = getMs(CURLINFO_PRETRANSFER_TIME),
            .startTransferMs = getMs(CURLINFO_STARTTRANSFER_TIME),
            .totalMs = getMs(CURLINFO_TOTAL_TIME),
        };
    }

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
    RawCurlResponse::CurlTimings curlTimings;
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
        metric.curlTimings = response.timings;

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

struct CurlTimingSummary
{
    std::size_t samples = 0;
    RawCurlResponse::CurlTimings avg;
    RawCurlResponse::CurlTimings p95;
    RawCurlResponse::CurlTimings max;
};

struct PerfResult
{
    PerfSummary summary;
    CurlTimingSummary curlTimings;
    std::vector<std::string> errors;
};

struct PerfReportEntry
{
    std::string name;
    PerfSummary summary;
};

std::mutex& PerfReportMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<PerfReportEntry>& PerfReportEntries()
{
    static std::vector<PerfReportEntry> entries;
    return entries;
}

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

CurlTimingSummary BuildCurlTimingSummary(std::vector<RawCurlResponse::CurlTimings> timings)
{
    CurlTimingSummary summary;
    summary.samples = timings.size();
    if (timings.empty())
    {
        return summary;
    }

    auto summarize = [&timings](auto member)
    {
        std::vector<double> values;
        values.reserve(timings.size());
        for (const auto& timing : timings)
        {
            values.push_back(timing.*member);
        }

        std::sort(values.begin(), values.end());
        const double avg = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        const auto p95Index = static_cast<std::size_t>(
            std::min<double>(static_cast<double>(values.size() - 1), std::ceil(0.95 * static_cast<double>(values.size() - 1))));

        return std::array<double, 3>{avg, values[p95Index], values.back()};
    };

    const auto nameLookup = summarize(&RawCurlResponse::CurlTimings::nameLookupMs);
    const auto connect = summarize(&RawCurlResponse::CurlTimings::connectMs);
    const auto appConnect = summarize(&RawCurlResponse::CurlTimings::appConnectMs);
    const auto preTransfer = summarize(&RawCurlResponse::CurlTimings::preTransferMs);
    const auto startTransfer = summarize(&RawCurlResponse::CurlTimings::startTransferMs);
    const auto total = summarize(&RawCurlResponse::CurlTimings::totalMs);

    summary.avg = {
        .nameLookupMs = nameLookup[0],
        .connectMs = connect[0],
        .appConnectMs = appConnect[0],
        .preTransferMs = preTransfer[0],
        .startTransferMs = startTransfer[0],
        .totalMs = total[0],
    };
    summary.p95 = {
        .nameLookupMs = nameLookup[1],
        .connectMs = connect[1],
        .appConnectMs = appConnect[1],
        .preTransferMs = preTransfer[1],
        .startTransferMs = startTransfer[1],
        .totalMs = total[1],
    };
    summary.max = {
        .nameLookupMs = nameLookup[2],
        .connectMs = connect[2],
        .appConnectMs = appConnect[2],
        .preTransferMs = preTransfer[2],
        .startTransferMs = startTransfer[2],
        .totalMs = total[2],
    };

    return summary;
}

PerfResult BuildPerfResult(std::vector<double> latenciesMs, std::size_t success, std::size_t failure,
                           std::vector<std::string> errors, double wallMs,
                           std::vector<RawCurlResponse::CurlTimings> curlTimings = {})
{
    PerfResult result;

    result.summary.count = latenciesMs.size();
    result.summary.success = success;
    result.summary.failure = failure;
    result.summary.wallMs = wallMs;
    result.curlTimings = BuildCurlTimingSummary(std::move(curlTimings));

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

    {
        std::lock_guard<std::mutex> lock(PerfReportMutex());
        PerfReportEntries().push_back({name, s});
    }

    std::cout << std::fixed << std::setprecision(3) << "[perf] " << name << " | requests=" << s.count
              << " success=" << s.success << " failure=" << s.failure << " wall_ms=" << s.wallMs << " rps=" << s.rps
              << " min_ms=" << s.min << " avg_ms=" << s.avg << " p50_ms=" << s.p50 << " p90_ms=" << s.p90
              << " p95_ms=" << s.p95 << " p99_ms=" << s.p99 << " max_ms=" << s.max << std::endl;

    if (EnvFlag("HTTP_PERF_WARN_SMALL_SAMPLE", true) && s.count > 0 && s.count < 100)
    {
        std::cout << "[perf warning] " << name << " has only " << s.count
                  << " samples; p95/p99 are useful signals but weak hard thresholds." << std::endl;
    }

    if (EnvFlag("HTTP_PERF_VERBOSE_CURL_TIMINGS") && result.curlTimings.samples > 0)
    {
        const auto& t = result.curlTimings;
        std::cout << std::fixed << std::setprecision(3) << "[perf curl] " << name << " | samples=" << t.samples
                  << " connect_avg_ms=" << t.avg.connectMs << " connect_p95_ms=" << t.p95.connectMs
                  << " ttfb_avg_ms=" << t.avg.startTransferMs << " ttfb_p95_ms=" << t.p95.startTransferMs
                  << " total_avg_ms=" << t.avg.totalMs << " total_p95_ms=" << t.p95.totalMs
                  << " namelookup_avg_ms=" << t.avg.nameLookupMs
                  << " pretransfer_avg_ms=" << t.avg.preTransferMs
                  << " appconnect_avg_ms=" << t.avg.appConnectMs << std::endl;
    }
}

void PrintFinalTimingRecap()
{
    std::vector<PerfReportEntry> entries;
    {
        std::lock_guard<std::mutex> lock(PerfReportMutex());
        entries = PerfReportEntries();
    }

    if (entries.empty())
    {
        return;
    }

    std::size_t totalRequests = 0;
    std::size_t totalSuccess = 0;
    std::size_t totalFailure = 0;
    double totalWallMs = 0.0;
    double weightedAvgLatency = 0.0;
    double maxP99 = 0.0;
    double maxLatency = 0.0;

    for (const auto& entry : entries)
    {
        const auto& s = entry.summary;
        totalRequests += s.count;
        totalSuccess += s.success;
        totalFailure += s.failure;
        totalWallMs += s.wallMs;
        weightedAvgLatency += s.avg * static_cast<double>(s.count);
        maxP99 = std::max(maxP99, s.p99);
        maxLatency = std::max(maxLatency, s.max);
    }

    if (totalRequests > 0)
    {
        weightedAvgLatency /= static_cast<double>(totalRequests);
    }

    auto byP99 = entries;
    std::sort(byP99.begin(), byP99.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.summary.p99 > rhs.summary.p99; });

    auto byRps = entries;
    std::sort(byRps.begin(), byRps.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.summary.rps < rhs.summary.rps; });

    std::cout << "\n[perf recap] request timing summary\n";
    std::cout << "[perf recap] scenarios=" << entries.size() << " requests=" << totalRequests
              << " success=" << totalSuccess << " failure=" << totalFailure << " total_wall_ms=" << std::fixed
              << std::setprecision(3) << totalWallMs << " weighted_avg_ms=" << weightedAvgLatency
              << " worst_p99_ms=" << maxP99 << " worst_max_ms=" << maxLatency << "\n";

    std::cout << "[perf recap] per-scenario timings\n";
    std::cout << std::left << std::setw(64) << "name" << std::right << std::setw(10) << "req" << std::setw(10) << "ok"
              << std::setw(10) << "fail" << std::setw(12) << "wall" << std::setw(12) << "rps" << std::setw(10) << "avg"
              << std::setw(10) << "p50" << std::setw(10) << "p90" << std::setw(10) << "p95" << std::setw(10) << "p99"
              << std::setw(10) << "max" << "\n";

    for (const auto& entry : entries)
    {
        const auto& s = entry.summary;
        std::cout << std::left << std::setw(64) << entry.name << std::right << std::setw(10) << s.count << std::setw(10)
                  << s.success << std::setw(10) << s.failure << std::setw(12) << s.wallMs << std::setw(12) << s.rps
                  << std::setw(10) << s.avg << std::setw(10) << s.p50 << std::setw(10) << s.p90 << std::setw(10)
                  << s.p95 << std::setw(10) << s.p99 << std::setw(10) << s.max << "\n";
    }

    const auto limit = std::min<std::size_t>(5, byP99.size());
    std::cout << "[perf recap] slowest p99 scenarios\n";
    for (std::size_t i = 0; i < limit; ++i)
    {
        const auto& entry = byP99[i];
        std::cout << "  #" << (i + 1) << " " << entry.name << " p99_ms=" << entry.summary.p99
                  << " p95_ms=" << entry.summary.p95 << " max_ms=" << entry.summary.max << "\n";
    }

    std::cout << "[perf recap] lowest throughput scenarios\n";
    for (std::size_t i = 0; i < limit; ++i)
    {
        const auto& entry = byRps[i];
        std::cout << "  #" << (i + 1) << " " << entry.name << " rps=" << entry.summary.rps
                  << " wall_ms=" << entry.summary.wallMs << " requests=" << entry.summary.count << "\n";
    }
}

class PerformanceTimingEnvironment : public ::testing::Environment
{
  public:
    void TearDown() override
    {
        PrintFinalTimingRecap();
    }
};

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

void ExpectLatencyWithinSampleAwareLimit(const PerfResult& result, double maxP99Ms)
{
    if (maxP99Ms <= 0.0)
    {
        return;
    }

    if (result.summary.count >= 100)
    {
        EXPECT_LE(result.summary.p99, maxP99Ms);
        return;
    }

    EXPECT_LE(result.summary.max, maxP99Ms * 2.0);
}

PerfResult RunSequentialPing(const std::string& baseUrl, int requestCount, ConnectionMode connectionMode)
{
    PerfCurlClient client(connectionMode);

    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(requestCount));
    std::vector<RawCurlResponse::CurlTimings> curlTimings;
    curlTimings.reserve(static_cast<std::size_t>(requestCount));

    std::size_t success = 0;
    std::size_t failure = 0;
    std::vector<std::string> errors;

    const auto wallBegin = Clock::now();

    for (int i = 0; i < requestCount; ++i)
    {
        auto metric = PerformPing(client, baseUrl);

        latencies.push_back(metric.latencyMs);
        curlTimings.push_back(metric.curlTimings);

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

    return BuildPerfResult(std::move(latencies), success, failure, std::move(errors), ElapsedMs(wallBegin, wallEnd),
                           std::move(curlTimings));
}

struct WorkerResult
{
    std::vector<double> latencies;
    std::vector<RawCurlResponse::CurlTimings> curlTimings;
    std::size_t success = 0;
    std::size_t failure = 0;
    std::vector<std::string> errors;
};

[[maybe_unused]] WorkerResult RunWorkerFixedRequests(const std::string& baseUrl, int requestCount,
                                                     ConnectionMode connectionMode)
{
    PerfCurlClient client(connectionMode);

    WorkerResult result;
    result.latencies.reserve(static_cast<std::size_t>(requestCount));
    result.curlTimings.reserve(static_cast<std::size_t>(requestCount));

    for (int i = 0; i < requestCount; ++i)
    {
        auto metric = PerformPing(client, baseUrl);

        result.latencies.push_back(metric.latencyMs);
        result.curlTimings.push_back(metric.curlTimings);

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
    std::vector<RawCurlResponse::CurlTimings> curlTimings;
    std::size_t success = 0;
    std::size_t failure = 0;
    std::vector<std::string> errors;

    for (auto& worker : workerResults)
    {
        success += worker.success;
        failure += worker.failure;

        latencies.insert(latencies.end(), worker.latencies.begin(), worker.latencies.end());
        curlTimings.insert(curlTimings.end(), worker.curlTimings.begin(), worker.curlTimings.end());

        for (const auto& error : worker.errors)
        {
            if (errors.size() < 10)
            {
                errors.push_back(error);
            }
        }
    }

    return BuildPerfResult(std::move(latencies), success, failure, std::move(errors), wallMs, std::move(curlTimings));
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
                                         result.curlTimings.reserve(static_cast<std::size_t>(requestsPerThread));

                                         for (int request = 0; request < requestsPerThread; ++request)
                                         {
                                             auto metric = PerformPing(client, baseUrl);

                                             result.latencies.push_back(metric.latencyMs);
                                             result.curlTimings.push_back(metric.curlTimings);

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
                                             result.curlTimings.push_back(metric.curlTimings);

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

void CloseRawSocket(RawSocket socket)
{
    if (socket == InvalidRawSocket)
    {
        return;
    }

#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

class RawSocketGuard
{
  public:
    explicit RawSocketGuard(RawSocket socket = InvalidRawSocket) : m_socket(socket) {}

    RawSocketGuard(const RawSocketGuard&) = delete;
    RawSocketGuard& operator=(const RawSocketGuard&) = delete;

    RawSocketGuard(RawSocketGuard&& other) noexcept : m_socket(std::exchange(other.m_socket, InvalidRawSocket)) {}

    RawSocketGuard& operator=(RawSocketGuard&& other) noexcept
    {
        if (this != &other)
        {
            CloseRawSocket(m_socket);
            m_socket = std::exchange(other.m_socket, InvalidRawSocket);
        }
        return *this;
    }

    ~RawSocketGuard()
    {
        CloseRawSocket(m_socket);
    }

    RawSocket get() const
    {
        return m_socket;
    }

  private:
    RawSocket m_socket = InvalidRawSocket;
};

class EnvVarGuard
{
  public:
    EnvVarGuard(const char* name, const char* value) : m_name(name)
    {
        if (const char* previous = std::getenv(name))
        {
            m_previous = previous;
        }

#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    }

    ~EnvVarGuard()
    {
#ifdef _WIN32
        _putenv_s(m_name.c_str(), m_previous ? m_previous->c_str() : "");
#else
        if (m_previous)
        {
            setenv(m_name.c_str(), m_previous->c_str(), 1);
        }
        else
        {
            unsetenv(m_name.c_str());
        }
#endif
    }

  private:
    std::string m_name;
    std::optional<std::string> m_previous;
};

RawSocketGuard ConnectRawSocket(uint16_t port)
{
    EnsureCurlGlobalInitialized();

    RawSocket socketHandle = socket(AF_INET, SOCK_STREAM, 0);
    if (socketHandle == InvalidRawSocket)
    {
        throw std::runtime_error("socket() failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        CloseRawSocket(socketHandle);
        throw std::runtime_error("connect() failed");
    }

    return RawSocketGuard(socketHandle);
}

void SendAll(RawSocket socket, std::string_view data)
{
    std::size_t sentBytes = 0;
    while (sentBytes < data.size())
    {
#ifdef _WIN32
        const int sent = send(socket, data.data() + sentBytes, static_cast<int>(data.size() - sentBytes), 0);
#else
        const ssize_t sent = send(socket, data.data() + sentBytes, data.size() - sentBytes, MSG_NOSIGNAL);
#endif
        if (sent <= 0)
        {
            throw std::runtime_error("send() failed");
        }
        sentBytes += static_cast<std::size_t>(sent);
    }
}

std::optional<std::size_t> ContentLength(std::string_view response)
{
    const std::string_view header = "Content-Length:";
    const auto pos = response.find(header);
    if (pos == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto valueStart = response.find_first_not_of(" \t", pos + header.size());
    if (valueStart == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto valueEnd = response.find("\r\n", valueStart);
    std::size_t length = 0;
    std::from_chars(response.data() + valueStart,
                    response.data() + (valueEnd == std::string_view::npos ? response.size() : valueEnd), length);
    return length;
}

std::string ReadHttpResponse(RawSocket socket, std::chrono::milliseconds timeout)
{
    std::string response;
    auto deadline = Clock::now() + timeout;

    while (Clock::now() < deadline)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket, &readSet);

        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now());
        timeval tv{};
        tv.tv_sec = static_cast<long>(remaining.count() / 1000000);
        tv.tv_usec = static_cast<long>(remaining.count() % 1000000);

#ifdef _WIN32
        const int selected = select(0, &readSet, nullptr, nullptr, &tv);
#else
        const int selected = select(socket + 1, &readSet, nullptr, nullptr, &tv);
#endif
        if (selected <= 0)
        {
            continue;
        }

        std::array<char, 4096> buffer{};
        const int received = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0)
        {
            break;
        }

        response.append(buffer.data(), static_cast<std::size_t>(received));
        const auto headersEnd = response.find("\r\n\r\n");
        if (headersEnd != std::string::npos)
        {
            const auto length = ContentLength(response);
            if (length && response.size() >= headersEnd + 4 + *length)
            {
                return response;
            }
        }
    }

    return response;
}

bool WaitForPeerClose(RawSocket socket, std::chrono::milliseconds timeout)
{
    const auto deadline = Clock::now() + timeout;

    while (Clock::now() < deadline)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket, &readSet);

        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now());
        timeval tv{};
        tv.tv_sec = static_cast<long>(remaining.count() / 1000000);
        tv.tv_usec = static_cast<long>(remaining.count() % 1000000);

#ifdef _WIN32
        const int selected = select(0, &readSet, nullptr, nullptr, &tv);
#else
        const int selected = select(socket + 1, &readSet, nullptr, nullptr, &tv);
#endif
        if (selected <= 0)
        {
            continue;
        }

        char byte = 0;
        const int received = recv(socket, &byte, 1, MSG_PEEK);
        if (received <= 0)
        {
            return true;
        }
        return false;
    }

    return false;
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

    uint16_t getPort() const
    {
        return server->port();
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
    // if (EnvInt("HTTP_PERF_ENABLE_CONNECTION_CHURN", 0, 0) == 0)
    // {
    //     GTEST_SKIP() << "Disabled by default because connection churn can expose slow close/keep-alive handling. "
    //                  << "Set HTTP_PERF_ENABLE_CONNECTION_CHURN=1 to enable it.";
    // }

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

TEST_F(ServerPerformanceTest, ConcurrentKeepAliveEightClientsReceiveAllResponses)
{
    auto result = RunConcurrentPing(getBaseUrl(), 8, 50, ConnectionMode::KeepAlive);

    ExpectHealthyResult("concurrent_keep_alive_8_clients_50_requests", result);
    EXPECT_EQ(result.summary.count, 400u);
}

TEST_F(ServerPerformanceTest, ConnectionCloseConcurrentShortConnections)
{
    auto result = RunConcurrentPing(getBaseUrl(), 8, 5, ConnectionMode::NewConnectionPerRequest);

    ExpectHealthyResult("connection_close_8_clients_5_requests", result);

    const double maxP99Ms = EnvDouble("HTTP_PERF_CONNECTION_CLOSE_MAX_P99_MS", 25.0, 0.0);
    ExpectLatencyWithinSampleAwareLimit(result, maxP99Ms);
}

TEST_F(ServerPerformanceTest, IdleKeepAliveConnectionsDoNotStarvePing)
{
    std::vector<RawSocketGuard> idleConnections;
    idleConnections.reserve(8);

    for (int i = 0; i < 8; ++i)
    {
        idleConnections.push_back(ConnectRawSocket(getPort()));
    }

    auto result = RunConcurrentPing(getBaseUrl(), 8, 10, ConnectionMode::KeepAlive);

    ExpectHealthyResult("ping_while_8_idle_connections_are_open", result);

    const double maxP99Ms = EnvDouble("HTTP_PERF_IDLE_KEEPALIVE_MAX_P99_MS", 25.0, 0.0);
    ExpectLatencyWithinSampleAwareLimit(result, maxP99Ms);
}

TEST_F(ServerPerformanceTest, MaxRequestsClosesConnectionAfterLimit)
{
    EnvVarGuard maxRequests("HTTP_SERVER_MAX_REQUESTS_PER_CONNECTION", "2");
    auto socket = ConnectRawSocket(getPort());

    const std::string request = "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n";

    SendAll(socket.get(), request);
    const auto firstResponse = ReadHttpResponse(socket.get(), std::chrono::milliseconds(2000));
    ASSERT_NE(firstResponse.find("200 OK"), std::string::npos) << firstResponse;
    ASSERT_NE(firstResponse.find("Connection: keep-alive"), std::string::npos) << firstResponse;

    SendAll(socket.get(), request);
    const auto secondResponse = ReadHttpResponse(socket.get(), std::chrono::milliseconds(2000));
    ASSERT_NE(secondResponse.find("200 OK"), std::string::npos) << secondResponse;
    ASSERT_NE(secondResponse.find("Connection: close"), std::string::npos) << secondResponse;
    EXPECT_TRUE(WaitForPeerClose(socket.get(), std::chrono::milliseconds(2000)));
}

TEST_F(ServerPerformanceTest, KeepAliveIdleTimeoutClosesIdleConnection)
{
    EnvVarGuard keepAliveTimeout("HTTP_SERVER_KEEP_ALIVE_TIMEOUT_MS", "150");
    auto socket = ConnectRawSocket(getPort());

    SendAll(socket.get(), "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n");
    const auto response = ReadHttpResponse(socket.get(), std::chrono::milliseconds(2000));
    ASSERT_NE(response.find("200 OK"), std::string::npos) << response;
    ASSERT_NE(response.find("Connection: keep-alive"), std::string::npos) << response;

    EXPECT_TRUE(WaitForPeerClose(socket.get(), std::chrono::milliseconds(2000)));
}

TEST_F(ServerPerformanceTest, BurstManyClientsAtSameTime)
{
    const int concurrency = EnvInt("HTTP_PERF_BURST_CONCURRENCY", 64);

    auto result = RunConcurrentPing(getBaseUrl(), concurrency, 1, ConnectionMode::KeepAlive);

    ExpectHealthyResult("burst_" + std::to_string(concurrency) + "_clients", result);

    const double maxP99Ms = EnvDouble("HTTP_PERF_BURST_MAX_P99_MS", 25.0, 0.0);
    ExpectLatencyWithinSampleAwareLimit(result, maxP99Ms);
}

TEST_F(ServerPerformanceTest, ConcurrentConnectionChurn)
{
    // if (EnvInt("HTTP_PERF_ENABLE_CONNECTION_CHURN", 0, 0) == 0)
    //  {
    //     GTEST_SKIP() << "Disabled by default because connection churn is intentionally aggressive. "
    //                  << "Set HTTP_PERF_ENABLE_CONNECTION_CHURN=1 to enable it.";
    // }

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
    testing::AddGlobalTestEnvironment(new PerformanceTimingEnvironment);
    return RUN_ALL_TESTS();
}

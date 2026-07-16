#define NOMINMAX

#include "support/TestServer.hpp"

#include <curl/curl.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
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
#include <string_view>
#include <system_error>
#include <thread>
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

    // No curl_global_cleanup on purpose in the test binary: it avoids destruction-order issues
    // with possible curl singletons used by other tests.
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

bool EnvFlag(const char* name, bool defaultValue = false)
{
    const char* value = std::getenv(name);
    if (!value || std::string(value).empty())
    {
        return defaultValue;
    }

    const std::string text = value;
    return text != "0" && text != "false" && text != "FALSE" && text != "off" && text != "OFF";
}

double ElapsedMs(Clock::time_point begin, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::string Truncate(const std::string& value, std::size_t maxSize = 256)
{
    if (value.size() <= maxSize)
    {
        return value;
    }

    return value.substr(0, maxSize) + "...";
}

std::string EscapeForLog(std::string_view value, std::size_t maxSize = 160)
{
    std::ostringstream stream;
    const std::size_t limit = std::min(value.size(), maxSize);

    for (std::size_t i = 0; i < limit; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        switch (c)
        {
        case '\r':
            stream << "\\r";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (std::isprint(c))
            {
                stream << static_cast<char>(c);
            }
            else
            {
                stream << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec
                       << std::setfill(' ');
            }
            break;
        }
    }

    if (value.size() > maxSize)
    {
        stream << "...";
    }

    return stream.str();
}

std::string ToLowerAscii(std::string_view value)
{
    std::string output;
    output.reserve(value.size());
    for (const char c : value)
    {
        output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return output;
}

std::size_t CountOccurrences(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
    {
        return 0;
    }

    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string_view::npos)
    {
        ++count;
        pos += needle.size();
    }
    return count;
}

struct CurlResponse
{
    CURLcode curlCode = CURLE_OK;
    long httpCode = 0;
    std::string body;
    std::string error;
    double totalMs = 0.0;
};

class RobustCurlClient
{
  public:
    RobustCurlClient()
    {
        EnsureCurlGlobalInitialized();

        m_curl = curl_easy_init();
        if (m_curl == nullptr)
        {
            throw std::runtime_error("curl_easy_init failed");
        }

        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &RobustCurlClient::WriteBody);
        curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(m_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(m_curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

        const long connectTimeoutMs = EnvInt("HTTP_ROBUST_CONNECT_TIMEOUT_MS", 2000);
        const long requestTimeoutMs = EnvInt("HTTP_ROBUST_REQUEST_TIMEOUT_MS", 3000);

        curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs);
        curl_easy_setopt(m_curl, CURLOPT_TIMEOUT_MS, requestTimeoutMs);
    }

    ~RobustCurlClient()
    {
        if (m_curl != nullptr)
        {
            curl_easy_cleanup(m_curl);
        }
    }

    RobustCurlClient(const RobustCurlClient&) = delete;
    RobustCurlClient& operator=(const RobustCurlClient&) = delete;

    CurlResponse Get(const std::string& url)
    {
        CurlResponse response;
        char errorBuffer[CURL_ERROR_SIZE] = {};

        const auto begin = Clock::now();

        curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(m_curl, CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, nullptr);
        curl_easy_setopt(m_curl, CURLOPT_FRESH_CONNECT, 0L);
        curl_easy_setopt(m_curl, CURLOPT_FORBID_REUSE, 0L);

        response.curlCode = curl_easy_perform(m_curl);
        response.totalMs = ElapsedMs(begin, Clock::now());
        curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &response.httpCode);

        if (response.curlCode != CURLE_OK)
        {
            response.error = errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(response.curlCode);
        }

        return response;
    }

  private:
    static std::size_t WriteBody(void* data, std::size_t size, std::size_t nmemb, void* userData)
    {
        const std::size_t totalSize = size * nmemb;
        auto* body = static_cast<std::string*>(userData);
        body->append(static_cast<const char*>(data), totalSize);
        return totalSize;
    }

    CURL* m_curl = nullptr;
};

struct PingMetric
{
    bool ok = false;
    long code = 0;
    double latencyMs = 0.0;
    std::string error;
};

PingMetric PerformPing(const std::string& baseUrl)
{
    PingMetric metric;

    try
    {
        RobustCurlClient client;
        const auto response = client.Get(baseUrl + "/ping");
        metric.code = response.httpCode;
        metric.latencyMs = response.totalMs;
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
    }
    catch (const std::exception& e)
    {
        metric.ok = false;
        metric.error = std::string("exception=") + e.what();
    }
    catch (...)
    {
        metric.ok = false;
        metric.error = "unknown exception";
    }

    return metric;
}

struct PingRunResult
{
    std::size_t count = 0;
    std::size_t success = 0;
    std::size_t failure = 0;
    double wallMs = 0.0;
    double minMs = 0.0;
    double avgMs = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
    std::vector<std::string> errors;
};

PingRunResult BuildPingRunResult(std::vector<PingMetric> metrics, double wallMs)
{
    PingRunResult result;
    result.count = metrics.size();
    result.wallMs = wallMs;

    std::vector<double> latencies;
    latencies.reserve(metrics.size());

    for (const auto& metric : metrics)
    {
        latencies.push_back(metric.latencyMs);
        if (metric.ok)
        {
            ++result.success;
        }
        else
        {
            ++result.failure;
            if (result.errors.size() < 10)
            {
                result.errors.push_back(metric.error);
            }
        }
    }

    if (!latencies.empty())
    {
        std::sort(latencies.begin(), latencies.end());
        result.minMs = latencies.front();
        result.maxMs = latencies.back();
        result.avgMs = std::accumulate(latencies.begin(), latencies.end(), 0.0) / static_cast<double>(latencies.size());
        const auto p95Index = static_cast<std::size_t>(std::min<double>(
            static_cast<double>(latencies.size() - 1), std::ceil(0.95 * static_cast<double>(latencies.size() - 1))));
        result.p95Ms = latencies[p95Index];
    }

    return result;
}

PingRunResult RunConcurrentPing(const std::string& baseUrl, int threadCount, int requestsPerThread)
{
    std::promise<void> startPromise;
    std::shared_future<void> startFuture(startPromise.get_future());

    std::vector<std::future<std::vector<PingMetric>>> futures;
    futures.reserve(static_cast<std::size_t>(threadCount));

    for (int i = 0; i < threadCount; ++i)
    {
        futures.push_back(std::async(std::launch::async,
                                     [baseUrl, startFuture, requestsPerThread]() mutable
                                     {
                                         startFuture.wait();

                                         std::vector<PingMetric> metrics;
                                         metrics.reserve(static_cast<std::size_t>(requestsPerThread));

                                         for (int request = 0; request < requestsPerThread; ++request)
                                         {
                                             metrics.push_back(PerformPing(baseUrl));
                                         }

                                         return metrics;
                                     }));
    }

    const auto wallBegin = Clock::now();
    startPromise.set_value();

    std::vector<PingMetric> metrics;
    metrics.reserve(static_cast<std::size_t>(threadCount * requestsPerThread));

    for (auto& future : futures)
    {
        auto workerMetrics = future.get();
        metrics.insert(metrics.end(), workerMetrics.begin(), workerMetrics.end());
    }

    return BuildPingRunResult(std::move(metrics), ElapsedMs(wallBegin, Clock::now()));
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

    explicit operator bool() const
    {
        return m_socket != InvalidRawSocket;
    }

  private:
    RawSocket m_socket = InvalidRawSocket;
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

bool TrySendAll(RawSocket socket, std::string_view data)
{
    try
    {
        SendAll(socket, data);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::optional<std::size_t> ContentLength(std::string_view response)
{
    const std::string lower = ToLowerAscii(response);
    constexpr std::string_view header = "content-length:";
    const auto pos = lower.find(header);
    if (pos == std::string::npos)
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
    const char* begin = response.data() + valueStart;
    const char* end = response.data() + (valueEnd == std::string_view::npos ? response.size() : valueEnd);
    const auto result = std::from_chars(begin, end, length);
    if (result.ec != std::errc())
    {
        return std::nullopt;
    }

    return length;
}

std::optional<int> HttpStatusCode(std::string_view response)
{
    if (response.size() < 12 || response.substr(0, 5) != "HTTP/")
    {
        return std::nullopt;
    }

    const auto firstSpace = response.find(' ');
    if (firstSpace == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto codeStart = firstSpace + 1;
    const auto codeEnd = response.find(' ', codeStart);
    if (codeEnd == std::string_view::npos || codeEnd <= codeStart)
    {
        return std::nullopt;
    }

    int code = 0;
    const auto result = std::from_chars(response.data() + codeStart, response.data() + codeEnd, code);
    if (result.ec != std::errc())
    {
        return std::nullopt;
    }

    return code;
}

bool HasHeaderValue(std::string_view response, std::string_view headerName, std::string_view expectedValue)
{
    const std::string lowerResponse = ToLowerAscii(response);
    const std::string lowerHeader = ToLowerAscii(headerName);
    const std::string lowerExpected = ToLowerAscii(expectedValue);

    std::size_t pos = 0;
    while ((pos = lowerResponse.find(lowerHeader, pos)) != std::string::npos)
    {
        const bool lineStart = pos == 0 || lowerResponse[pos - 1] == '\n';
        if (!lineStart)
        {
            pos += lowerHeader.size();
            continue;
        }

        const auto valueStart = lowerResponse.find_first_not_of(" \t", pos + lowerHeader.size());
        const auto lineEnd = lowerResponse.find("\r\n", pos);
        if (valueStart != std::string::npos && lineEnd != std::string::npos && valueStart < lineEnd)
        {
            const auto value = lowerResponse.substr(valueStart, lineEnd - valueStart);
            if (value.find(lowerExpected) != std::string::npos)
            {
                return true;
            }
        }

        pos += lowerHeader.size();
    }

    return false;
}

bool IsRejectedStatus(int code)
{
    return (code >= 400 && code < 500) || code == 501 || code == 505;
}

bool IsPingResponse(std::string_view response)
{
    const auto status = HttpStatusCode(response);
    return status && *status == 200 && response.find("pong") != std::string_view::npos;
}

struct RawReadResult
{
    std::string bytes;
    bool peerClosed = false;
    bool timedOut = false;
};

enum class ReadMode
{
    HeadersOnly,
    FullBodyWhenContentLengthIsKnown
};

RawReadResult ReadHttpResponse(RawSocket socket, std::chrono::milliseconds timeout,
                               ReadMode mode = ReadMode::HeadersOnly)
{
    RawReadResult result;
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
        if (selected < 0)
        {
            result.peerClosed = true;
            return result;
        }

        if (selected == 0)
        {
            continue;
        }

        std::array<char, 4096> buffer{};
        const int received = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0)
        {
            result.peerClosed = true;
            return result;
        }

        result.bytes.append(buffer.data(), static_cast<std::size_t>(received));
        const auto headersEnd = result.bytes.find("\r\n\r\n");
        if (headersEnd != std::string::npos)
        {
            if (mode == ReadMode::HeadersOnly)
            {
                return result;
            }

            const auto length = ContentLength(result.bytes);
            if (!length || result.bytes.size() >= headersEnd + 4 + *length)
            {
                return result;
            }
        }
    }

    result.timedOut = true;
    return result;
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

std::chrono::milliseconds RawTimeout()
{
    return std::chrono::milliseconds(EnvInt("HTTP_ROBUST_RAW_TIMEOUT_MS", 1500));
}

struct RobustnessScenarioResult
{
    std::string name;
    std::size_t sent = 0;
    std::size_t rejected = 0;
    std::size_t closed = 0;
    std::size_t unexpectedSuccess = 0;
    std::size_t unexpectedServerError = 0;
    std::size_t timeout = 0;
    std::vector<std::string> errors;
};

std::mutex& RobustnessReportMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<RobustnessScenarioResult>& RobustnessReportEntries()
{
    static std::vector<RobustnessScenarioResult> entries;
    return entries;
}

void RecordRobustnessResult(const RobustnessScenarioResult& result)
{
    std::lock_guard<std::mutex> lock(RobustnessReportMutex());
    RobustnessReportEntries().push_back(result);
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

void PrintRobustnessResult(const RobustnessScenarioResult& result)
{
    RecordRobustnessResult(result);

    std::cout << "[robustness] " << result.name << " | sent=" << result.sent << " rejected=" << result.rejected
              << " closed=" << result.closed << " unexpected_success=" << result.unexpectedSuccess
              << " unexpected_5xx=" << result.unexpectedServerError << " timeout=" << result.timeout << std::endl;
}

void PrintPingRunResult(const std::string& name, const PingRunResult& result)
{
    std::cout << std::fixed << std::setprecision(3) << "[robustness ping] " << name << " | requests=" << result.count
              << " success=" << result.success << " failure=" << result.failure << " wall_ms=" << result.wallMs
              << " min_ms=" << result.minMs << " avg_ms=" << result.avgMs << " p95_ms=" << result.p95Ms
              << " max_ms=" << result.maxMs << std::endl;
}

void PrintFinalRobustnessRecap()
{
    std::vector<RobustnessScenarioResult> entries;
    {
        std::lock_guard<std::mutex> lock(RobustnessReportMutex());
        entries = RobustnessReportEntries();
    }

    if (entries.empty())
    {
        return;
    }

    std::size_t sent = 0;
    std::size_t rejected = 0;
    std::size_t closed = 0;
    std::size_t unexpectedSuccess = 0;
    std::size_t unexpectedServerError = 0;
    std::size_t timeout = 0;

    for (const auto& entry : entries)
    {
        sent += entry.sent;
        rejected += entry.rejected;
        closed += entry.closed;
        unexpectedSuccess += entry.unexpectedSuccess;
        unexpectedServerError += entry.unexpectedServerError;
        timeout += entry.timeout;
    }

    std::cout << "\n[robustness recap] scenarios=" << entries.size() << " invalid_requests=" << sent
              << " rejected=" << rejected << " closed=" << closed << " unexpected_success=" << unexpectedSuccess
              << " unexpected_5xx=" << unexpectedServerError << " timeout=" << timeout << "\n";

    std::cout << std::left << std::setw(64) << "name" << std::right << std::setw(10) << "sent" << std::setw(10)
              << "reject" << std::setw(10) << "closed" << std::setw(10) << "2xx" << std::setw(10) << "5xx"
              << std::setw(10) << "timeout" << "\n";

    for (const auto& entry : entries)
    {
        std::cout << std::left << std::setw(64) << entry.name << std::right << std::setw(10) << entry.sent
                  << std::setw(10) << entry.rejected << std::setw(10) << entry.closed << std::setw(10)
                  << entry.unexpectedSuccess << std::setw(10) << entry.unexpectedServerError << std::setw(10)
                  << entry.timeout << "\n";
    }
}

class RobustnessEnvironment : public ::testing::Environment
{
  public:
    void TearDown() override
    {
        PrintFinalRobustnessRecap();
    }
};

struct RawCase
{
    std::string name;
    std::string request;
};

RawReadResult SendRawRequestAndRead(uint16_t port, std::string_view request, ReadMode mode = ReadMode::HeadersOnly)
{
    auto socket = ConnectRawSocket(port);
    if (!request.empty())
    {
        SendAll(socket.get(), request);
    }
    return ReadHttpResponse(socket.get(), RawTimeout(), mode);
}

RobustnessScenarioResult RunRejectedRawMatrix(uint16_t port, const std::string& name, const std::vector<RawCase>& cases)
{
    RobustnessScenarioResult result;
    result.name = name;

    for (const auto& testCase : cases)
    {
        ++result.sent;

        RawReadResult raw;
        try
        {
            raw = SendRawRequestAndRead(port, testCase.request);
        }
        catch (const std::exception& e)
        {
            ++result.closed;
            if (result.errors.size() < 10)
            {
                result.errors.push_back(testCase.name + ": exception=\"" + e.what() + "\"");
            }
            continue;
        }

        const auto status = HttpStatusCode(raw.bytes);
        if (status)
        {
            if (*status >= 200 && *status < 300)
            {
                ++result.unexpectedSuccess;
                if (result.errors.size() < 10)
                {
                    result.errors.push_back(testCase.name + ": unexpected 2xx response: " + EscapeForLog(raw.bytes));
                }
            }
            else if (*status >= 500 && *status < 600 && *status != 501 && *status != 505)
            {
                ++result.unexpectedServerError;
                if (result.errors.size() < 10)
                {
                    result.errors.push_back(testCase.name + ": unexpected 5xx response: " + EscapeForLog(raw.bytes));
                }
            }
            else if (IsRejectedStatus(*status))
            {
                ++result.rejected;
            }
            else
            {
                ++result.unexpectedSuccess;
                if (result.errors.size() < 10)
                {
                    result.errors.push_back(testCase.name + ": unexpected status " + std::to_string(*status) + ": " +
                                            EscapeForLog(raw.bytes));
                }
            }
        }
        else if (raw.peerClosed)
        {
            ++result.closed;
        }
        else
        {
            ++result.timeout;
            if (result.errors.size() < 10)
            {
                result.errors.push_back(testCase.name + ": no response/close before timeout. partial=\"" +
                                        EscapeForLog(raw.bytes) + "\"");
            }
        }
    }

    PrintRobustnessResult(result);
    return result;
}

void ExpectRejectedMatrixIsClean(const RobustnessScenarioResult& result)
{
    SCOPED_TRACE(result.name);
    EXPECT_EQ(result.unexpectedSuccess, 0u) << JoinErrors(result.errors);
    EXPECT_EQ(result.unexpectedServerError, 0u) << JoinErrors(result.errors);
    EXPECT_EQ(result.timeout, 0u) << JoinErrors(result.errors);
    EXPECT_EQ(result.rejected + result.closed, result.sent) << JoinErrors(result.errors);
}

std::string SimpleGet(std::string_view path, std::string_view connection = "close")
{
    std::ostringstream stream;
    stream << "GET " << path << " HTTP/1.1\r\nHost: localhost\r\nConnection: " << connection << "\r\n\r\n";
    return stream.str();
}

std::string PostWithBody(std::string_view path, std::string_view body, std::string_view connection = "close")
{
    std::ostringstream stream;
    stream << "POST " << path << " HTTP/1.1\r\nHost: localhost\r\nConnection: " << connection
           << "\r\nContent-Length: " << body.size() << "\r\n\r\n"
           << body;
    return stream.str();
}

void SendByteByByte(RawSocket socket, std::string_view data, std::chrono::milliseconds delay)
{
    for (char c : data)
    {
        SendAll(socket, std::string_view(&c, 1));
        std::this_thread::sleep_for(delay);
    }
}
} // namespace

class ServerRobustnessTest : public ::testing::Test
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

    uint16_t getPort() const
    {
        return server->port();
    }

    void ExpectPingOk() const
    {
        const auto metric = PerformPing(getBaseUrl());
        ASSERT_TRUE(metric.ok) << metric.error;
    }

    std::unique_ptr<TestServer> server;
};

TEST_F(ServerRobustnessTest, BaselineValidPingAndNotFound)
{
    ExpectPingOk();

    RobustCurlClient client;
    const auto notFound = client.Get(getBaseUrl() + "/this-does-not-exist");
    ASSERT_EQ(notFound.curlCode, CURLE_OK) << notFound.error;
    EXPECT_EQ(notFound.httpCode, 404);

    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, ConcurrentValidPingTraffic)
{
    const int threads = EnvInt("HTTP_ROBUST_VALID_CONCURRENCY", 10);
    const int requestsPerThread = EnvInt("HTTP_ROBUST_VALID_REQUESTS_PER_THREAD", 20);

    auto result = RunConcurrentPing(getBaseUrl(), threads, requestsPerThread);
    PrintPingRunResult("concurrent_valid_ping", result);

    EXPECT_EQ(result.failure, 0u) << JoinErrors(result.errors);
    EXPECT_EQ(result.success, result.count);
}

TEST_F(ServerRobustnessTest, MalformedRequestLineMatrix)
{
    const std::vector<RawCase> cases = {
        {"garbage", "THIS IS NOT HTTP\r\n\r\n"},
        {"empty_line_only", "\r\n\r\n"},
        {"missing_http_version", "GET /ping\r\n\r\n"},
        {"missing_target", "GET  HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {"invalid_version", "GET /ping XYZ\r\nHost: localhost\r\n\r\n"},
        {"extra_tokens", "GET /ping HTTP/1.1 unexpected\r\nHost: localhost\r\n\r\n"},
        {"http2_preface", "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"},
        {"binary_prefix", std::string("\x00\x01\x02\x03", 4) + "GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n"},
    };

    const auto result = RunRejectedRawMatrix(getPort(), "malformed_request_line_matrix", cases);
    ExpectRejectedMatrixIsClean(result);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, UnsupportedOrInvalidMethodMatrix)
{
    const std::vector<RawCase> cases = {
        {"bogus_method", "BOGUS /ping HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {"delete_ping", "DELETE /ping HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {"trace_ping", "TRACE /ping HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {"options_ping", "OPTIONS /ping HTTP/1.1\r\nHost: localhost\r\n\r\n"},
        {"post_ping_empty_body", PostWithBody("/ping", "")},
        {"very_long_method", std::string(512, 'G') + " /ping HTTP/1.1\r\nHost: localhost\r\n\r\n"},
    };

    const auto result = RunRejectedRawMatrix(getPort(), "unsupported_or_invalid_method_matrix", cases);
    ExpectRejectedMatrixIsClean(result);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, InvalidHeaderMatrix)
{
    const std::vector<RawCase> cases = {
        {"host_without_colon", "GET /ping HTTP/1.1\r\nHost localhost\r\n\r\n"},
        {"empty_header_name", "GET /ping HTTP/1.1\r\n: value\r\n\r\n"},
        {"header_without_colon", "GET /ping HTTP/1.1\r\nHost: localhost\r\nBadHeader\r\n\r\n"},
        {"space_before_colon", "GET /ping HTTP/1.1\r\nHost: localhost\r\nBad-Header : value\r\n\r\n"},
        {"control_char_in_header",
         std::string("GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Bad: value") + std::string("\x01", 1) + "\r\n\r\n"},
    };

    const auto result = RunRejectedRawMatrix(getPort(), "invalid_header_matrix", cases);
    ExpectRejectedMatrixIsClean(result);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, InvalidContentLengthMatrix)
{
    const std::vector<RawCase> cases = {
        {"non_numeric", "POST /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length: abc\r\n\r\n"},
        {"negative", "POST /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length: -1\r\n\r\n"},
        {"empty", "POST /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length:\r\n\r\n"},
        {"overflow", "POST /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length: 999999999999999999999999\r\n\r\n"},
        {"conflicting_duplicate",
         "POST /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\nContent-Length: 8\r\n\r\nabc"},
        {"chunked_and_content_length",
         "POST /ping HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\n\r\nabc"},
    };

    const auto result = RunRejectedRawMatrix(getPort(), "invalid_content_length_matrix", cases);
    ExpectRejectedMatrixIsClean(result);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, HugeHeaderIsRejectedQuickly)
{
    const int hugeHeaderSize = EnvInt("HTTP_ROBUST_HUGE_HEADER_SIZE", 64 * 1024);
    const std::string hugeHeader(static_cast<std::size_t>(hugeHeaderSize), 'A');

    const std::vector<RawCase> cases = {
        {"huge_single_header", "GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Huge: " + hugeHeader + "\r\n\r\n"},
    };

    const auto result = RunRejectedRawMatrix(getPort(), "huge_header_is_rejected_quickly", cases);
    ExpectRejectedMatrixIsClean(result);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, TooManyHeadersAreRejected)
{
    const int headerCount = EnvInt("HTTP_ROBUST_TOO_MANY_HEADERS", 1000);

    std::ostringstream request;
    request << "GET /ping HTTP/1.1\r\nHost: localhost\r\n";
    for (int i = 0; i < headerCount; ++i)
    {
        request << "X-Test-" << i << ": value\r\n";
    }
    request << "\r\n";

    const std::vector<RawCase> cases = {{"too_many_headers", request.str()}};

    const auto result = RunRejectedRawMatrix(getPort(), "too_many_headers_are_rejected", cases);
    ExpectRejectedMatrixIsClean(result);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, VeryLongRequestTargetIsRejected)
{
    const int pathSize = EnvInt("HTTP_ROBUST_LONG_PATH_SIZE", 16 * 1024);
    const std::string longPath(static_cast<std::size_t>(pathSize), 'a');

    const std::vector<RawCase> cases = {
        {"very_long_path", "GET /" + longPath + " HTTP/1.1\r\nHost: localhost\r\n\r\n"},
    };

    const auto result = RunRejectedRawMatrix(getPort(), "very_long_request_target_is_rejected", cases);
    ExpectRejectedMatrixIsClean(result);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, Http11RequestWithoutHostIsRejected)
{
    const std::vector<RawCase> cases = {
        {"http11_without_host", "GET /ping HTTP/1.1\r\nConnection: close\r\n\r\n"},
    };

    const auto result = RunRejectedRawMatrix(getPort(), "http11_without_host_is_rejected", cases);
    ExpectRejectedMatrixIsClean(result);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, HeaderNamesAreCaseInsensitiveForValidRequests)
{
    auto socket = ConnectRawSocket(getPort());
    SendAll(socket.get(), "GET /ping HTTP/1.1\r\nhost: localhost\r\ncOnNeCtIoN: close\r\n\r\n");

    const auto response = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::FullBodyWhenContentLengthIsKnown);
    ASSERT_TRUE(IsPingResponse(response.bytes)) << response.bytes;
    EXPECT_TRUE(response.peerClosed || HasHeaderValue(response.bytes, "connection:", "close") ||
                WaitForPeerClose(socket.get(), std::chrono::milliseconds(1000)))
        << response.bytes;

    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, ConnectionCloseHeaderIsRespected)
{
    auto socket = ConnectRawSocket(getPort());
    SendAll(socket.get(), SimpleGet("/ping", "close"));

    const auto response = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::FullBodyWhenContentLengthIsKnown);
    ASSERT_TRUE(IsPingResponse(response.bytes)) << response.bytes;
    EXPECT_TRUE(response.peerClosed || HasHeaderValue(response.bytes, "connection:", "close") ||
                WaitForPeerClose(socket.get(), std::chrono::milliseconds(1000)))
        << response.bytes;

    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, KeepAliveTwoSequentialRequestsOnSameSocket)
{
    auto socket = ConnectRawSocket(getPort());

    SendAll(socket.get(), SimpleGet("/ping", "keep-alive"));
    const auto first = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::FullBodyWhenContentLengthIsKnown);
    ASSERT_TRUE(IsPingResponse(first.bytes)) << first.bytes;

    SendAll(socket.get(), SimpleGet("/ping", "close"));
    const auto second = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::FullBodyWhenContentLengthIsKnown);
    ASSERT_TRUE(IsPingResponse(second.bytes)) << second.bytes;

    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, NotFoundDoesNotCorruptKeepAliveConnection)
{
    auto socket = ConnectRawSocket(getPort());
    SendAll(socket.get(), SimpleGet("/this-does-not-exist", "keep-alive") + SimpleGet("/ping", "close"));

    std::string data;
    bool peerClosed = false;
    const auto first = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::FullBodyWhenContentLengthIsKnown);
    data += first.bytes;
    peerClosed = peerClosed || first.peerClosed;

    if (!first.peerClosed)
    {
        const auto second = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::FullBodyWhenContentLengthIsKnown);
        data += second.bytes;
        peerClosed = peerClosed || second.peerClosed;
    }

    ASSERT_NE(data.find("404"), std::string::npos) << data;
    // Either the server keeps the connection and returns the next ping, or it closes cleanly after 404.
    EXPECT_TRUE(data.find("pong") != std::string::npos || peerClosed || HasHeaderValue(data, "connection:", "close"))
        << data;

    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, PipelinedRequestsAreHandledOrConnectionIsClosedCleanly)
{
    auto socket = ConnectRawSocket(getPort());
    SendAll(socket.get(), SimpleGet("/ping", "keep-alive") + SimpleGet("/ping", "close"));

    std::string data;
    const auto first = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::FullBodyWhenContentLengthIsKnown);
    data += first.bytes;

    if (!first.peerClosed)
    {
        const auto second = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::FullBodyWhenContentLengthIsKnown);
        data += second.bytes;
    }

    EXPECT_GE(CountOccurrences(data, "200"), 1u) << data;
    EXPECT_NE(data.find("pong"), std::string::npos) << data;
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, MalformedRequestClosesConnectionBeforeNextRequest)
{
    auto socket = ConnectRawSocket(getPort());
    SendAll(socket.get(), "BAD REQUEST\r\n\r\n" + SimpleGet("/ping", "close"));

    const auto response = ReadHttpResponse(socket.get(), RawTimeout(), ReadMode::HeadersOnly);
    const auto status = HttpStatusCode(response.bytes);

    ASSERT_TRUE((status && IsRejectedStatus(*status)) || response.peerClosed) << response.bytes;
    EXPECT_EQ(response.bytes.find("pong"), std::string::npos) << response.bytes;
    EXPECT_TRUE(response.peerClosed || HasHeaderValue(response.bytes, "connection:", "close") ||
                WaitForPeerClose(socket.get(), std::chrono::milliseconds(1000)))
        << response.bytes;

    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, PartialHeaderConnectionsDoNotStarveValidRequests)
{
    const int connectionCount = EnvInt("HTTP_ROBUST_PARTIAL_HEADER_CONNECTIONS", 16);
    std::vector<RawSocketGuard> sockets;
    sockets.reserve(static_cast<std::size_t>(connectionCount));

    for (int i = 0; i < connectionCount; ++i)
    {
        auto socket = ConnectRawSocket(getPort());
        SendAll(socket.get(), "GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Slow: ");
        sockets.push_back(std::move(socket));
    }

    auto result = RunConcurrentPing(getBaseUrl(), EnvInt("HTTP_ROBUST_HEALTH_CONCURRENCY", 8),
                                    EnvInt("HTTP_ROBUST_HEALTH_REQUESTS_PER_THREAD", 5));
    PrintPingRunResult("ping_while_partial_headers_are_open", result);

    EXPECT_EQ(result.failure, 0u) << JoinErrors(result.errors);
    EXPECT_EQ(result.success, result.count);
}

TEST_F(ServerRobustnessTest, IncompleteBodyConnectionsDoNotStarveValidRequests)
{
    const int connectionCount = EnvInt("HTTP_ROBUST_INCOMPLETE_BODY_CONNECTIONS", 16);
    std::vector<RawSocketGuard> sockets;
    sockets.reserve(static_cast<std::size_t>(connectionCount));

    for (int i = 0; i < connectionCount; ++i)
    {
        auto socket = ConnectRawSocket(getPort());
        SendAll(socket.get(), "POST /ping HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1000000\r\n\r\nabc");
        sockets.push_back(std::move(socket));
    }

    auto result = RunConcurrentPing(getBaseUrl(), EnvInt("HTTP_ROBUST_HEALTH_CONCURRENCY", 8),
                                    EnvInt("HTTP_ROBUST_HEALTH_REQUESTS_PER_THREAD", 5));
    PrintPingRunResult("ping_while_incomplete_bodies_are_open", result);

    EXPECT_EQ(result.failure, 0u) << JoinErrors(result.errors);
    EXPECT_EQ(result.success, result.count);
}

TEST_F(ServerRobustnessTest, AbruptClientDisconnectFloodDoesNotBreakServer)
{
    const int concurrency = EnvInt("HTTP_ROBUST_ABRUPT_DISCONNECT_CONCURRENCY", 32);
    const int disconnectsPerThread = EnvInt("HTTP_ROBUST_ABRUPT_DISCONNECTS_PER_THREAD", 10);

    std::promise<void> startPromise;
    std::shared_future<void> startFuture(startPromise.get_future());
    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<std::size_t>(concurrency));

    for (int i = 0; i < concurrency; ++i)
    {
        futures.push_back(std::async(std::launch::async,
                                     [this, startFuture, disconnectsPerThread]() mutable
                                     {
                                         startFuture.wait();
                                         for (int request = 0; request < disconnectsPerThread; ++request)
                                         {
                                             auto socket = ConnectRawSocket(getPort());
                                             TrySendAll(socket.get(), "GET /ping HTTP/1.1\r\nHost: localhost\r\n");
                                             // RawSocketGuard closes immediately here.
                                         }
                                     }));
    }

    startPromise.set_value();
    for (auto& future : futures)
    {
        future.get();
    }

    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, MalformedRequestFloodDoesNotBreakServer)
{
    const int concurrency = EnvInt("HTTP_ROBUST_MALFORMED_FLOOD_CONCURRENCY", 8);
    const int requestsPerThread = EnvInt("HTTP_ROBUST_MALFORMED_FLOOD_REQUESTS_PER_THREAD", 25);

    std::atomic<int> completed{0};
    std::promise<void> startPromise;
    std::shared_future<void> startFuture(startPromise.get_future());
    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<std::size_t>(concurrency));

    for (int i = 0; i < concurrency; ++i)
    {
        futures.push_back(std::async(std::launch::async,
                                     [this, startFuture, requestsPerThread, &completed]() mutable
                                     {
                                         startFuture.wait();
                                         for (int request = 0; request < requestsPerThread; ++request)
                                         {
                                             auto socket = ConnectRawSocket(getPort());
                                             TrySendAll(socket.get(), "THIS IS NOT HTTP\r\n\r\n");
                                             (void)ReadHttpResponse(socket.get(), std::chrono::milliseconds(500));
                                             ++completed;
                                         }
                                     }));
    }

    startPromise.set_value();

    auto health = RunConcurrentPing(getBaseUrl(), EnvInt("HTTP_ROBUST_HEALTH_CONCURRENCY", 4),
                                    EnvInt("HTTP_ROBUST_HEALTH_REQUESTS_PER_THREAD", 10));
    PrintPingRunResult("ping_during_malformed_request_flood", health);

    for (auto& future : futures)
    {
        future.get();
    }

    EXPECT_EQ(completed.load(), concurrency * requestsPerThread);
    EXPECT_EQ(health.failure, 0u) << JoinErrors(health.errors);
    EXPECT_EQ(health.success, health.count);
    ExpectPingOk();
}

TEST_F(ServerRobustnessTest, VerySlowHeaderDripDoesNotBlockServer)
{
    const int byteDelayMs = EnvInt("HTTP_ROBUST_DRIP_BYTE_DELAY_MS", 20, 0);
    const auto dripDelay = std::chrono::milliseconds(byteDelayMs);

    auto slowClient = std::async(std::launch::async,
                                 [this, dripDelay]
                                 {
                                     RawReadResult result;
                                     try
                                     {
                                         auto socket = ConnectRawSocket(getPort());
                                         SendByteByByte(socket.get(), SimpleGet("/ping", "close"), dripDelay);
                                         return ReadHttpResponse(socket.get(), std::chrono::milliseconds(3000),
                                                                 ReadMode::FullBodyWhenContentLengthIsKnown);
                                     }
                                     catch (...)
                                     {
                                         result.peerClosed = true;
                                         return result;
                                     }
                                 });

    auto health = RunConcurrentPing(getBaseUrl(), EnvInt("HTTP_ROBUST_HEALTH_CONCURRENCY", 4),
                                    EnvInt("HTTP_ROBUST_HEALTH_REQUESTS_PER_THREAD", 5));
    PrintPingRunResult("ping_while_slow_header_drip_is_running", health);

    const auto slowResponse = slowClient.get();
    const auto status = HttpStatusCode(slowResponse.bytes);

    // Either the request is accepted because it completed before the server request timeout,
    // or it is rejected/closed. The important part is that it does not starve healthy traffic.
    EXPECT_TRUE(IsPingResponse(slowResponse.bytes) || (status && IsRejectedStatus(*status)) || slowResponse.peerClosed)
        << slowResponse.bytes;
    EXPECT_EQ(health.failure, 0u) << JoinErrors(health.errors);
    EXPECT_EQ(health.success, health.count);
    ExpectPingOk();
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new RobustnessEnvironment);
    return RUN_ALL_TESTS();
}

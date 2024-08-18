#pragma once
#include <curl/curl.h>
#include <string_view>
#include <format>
#include <map>

struct NetworkResponse
{
    std::map<std::string, std::string> headers;
    uint32_t code;
    std::vector<uint8_t> memory;
};

template <>
struct std::formatter<NetworkResponse> : std::formatter<string_view>
{
    auto format(const NetworkResponse &response, std::format_context &ctx) const
    {
        std::string temp = "Headers : \n";

        for (const auto &header : response.headers)
            std::format_to(std::back_inserter(temp), "  {} : {}\n", header.first, header.second);

        std::format_to(std::back_inserter(temp), "Code : {}\n", response.code);

        temp += "Body : \n\t" + std::string(response.memory.begin(), response.memory.end()) + "\n";

        return std::formatter<string_view>::format(temp, ctx);
    }
};

class NetworkCurl
{
public:
    static NetworkCurl &GetInstance();
    ~NetworkCurl();
    void EnableDebug();
    void DisableDebug();
    NetworkResponse Get(std::string_view URL);
    CURL *operator*() { return m_curl; };

private:
    NetworkCurl();
    static size_t m_FillNetworkResponse(void *data, size_t size, size_t nmemb, void *networkResponse);

    CURL *m_curl;
};

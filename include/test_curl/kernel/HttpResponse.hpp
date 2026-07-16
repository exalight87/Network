#pragma once
#include <unordered_map>
#include <string>
#include <limits>
#include <cstdint>
#include <test_curl/kernel/HttpPage.hpp>
#include <variant>

struct HttpResponse
{
    std::unordered_map<std::string, std::string> headers = {};
    uint32_t code = (std::numeric_limits<uint32_t>::max)();
    std::variant<std::string, HttpPage> body;

    std::string format(bool includeBody = true) const;
    bool loadFile(const std::string& filepath);
    bool loadFileFrom(const std::string& baseDir, const std::string& filepath);

    bool empty() const { return code == (std::numeric_limits<uint32_t>::max)(); }
    void reset() { code = (std::numeric_limits<uint32_t>::max)(); body = ""; }

    static HttpResponse CODE_404;
    static HttpResponse CLOSE_CONNECTION;
    static HttpResponse OPEN_CONNECTION;
};

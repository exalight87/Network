#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <iostream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <test_curl/kernel/HttpRequest.hpp>
#include <unordered_map>

namespace
{
HttpRequest::Methods _GetMethodFromString(std::string_view methodStr);
URL::Protocols _GetProtocolFromString(std::string_view protocoldStr);
std::string _UrlDecode(std::string_view encoded);
bool _ParseRequestLine(std::string_view requestLine, std::string_view& method, std::string_view& target,
                       std::string_view& version);
bool _ValidateHttpVersion(std::string_view version);
bool _ValidateHeaderName(std::string_view name);
bool _ValidateHeaderValue(std::string_view value);
bool _ParseContentLength(std::string_view value, std::size_t& contentLength);
bool _IEquals(std::string_view lhs, std::string_view rhs);
std::string _Trim(std::string_view value);
std::string _ToUpper(std::string_view value);
} // namespace

void URL::parse(std::string_view url)
{
    protocol = Protocols::UNKNOWN;
    domain.clear();
    path.clear();
    queryParams.clear();

    // absolute uri
    if (!url.starts_with('/'))
    {
        // PROTOCOL
        std::string_view domainDelim = "://";
        auto protocolSize = url.find(domainDelim);
        if (protocolSize == std::string::npos)
        {
            path = url.empty() ? "/" : std::string(url);
            return;
        }

        protocol = _GetProtocolFromString(url.substr(0, protocolSize));
        url.remove_prefix(protocolSize + domainDelim.size());

        // DOMAIN
        std::string_view pathDelim = "/";
        auto domainSize = url.find(pathDelim);
        if (domainSize == std::string::npos)
        {
            domain = std::string(url);
            path = "/";
            return;
        }

        domain = url.substr(0, domainSize);
        url.remove_prefix(domainSize);
    }

    // PATH
    std::string_view queryArgsDelim = "?";
    bool isQueryArgs = true;
    auto pathSize = url.find_first_of(queryArgsDelim);
    if (pathSize == std::string::npos)
    {
        isQueryArgs = false;
        pathSize = url.size();
    }

    path = url.substr(0, pathSize);
    if (path.empty())
    {
        path = "/";
    }
    url.remove_prefix(pathSize + (isQueryArgs ? queryArgsDelim.size() : 0));

    if (!isQueryArgs || url.empty())
    {
        return;
    }

    // QUERY PARAMS
    std::size_t start = 0;
    while (start <= url.size())
    {
        const auto end = url.find('&', start);
        const auto queryArg = url.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!queryArg.empty())
        {
            const auto equalPos = queryArg.find('=');
            const auto key = equalPos == std::string_view::npos ? queryArg : queryArg.substr(0, equalPos);
            const auto value = equalPos == std::string_view::npos ? std::string_view{} : queryArg.substr(equalPos + 1);

            queryParams.emplace(_UrlDecode(key), _UrlDecode(value));
        }

        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
}

bool HttpRequest::parse(std::string_view request)
{
    using namespace std::literals;
    method = Methods::UNKNOWN;
    url = {};
    httpVersion.clear();
    headers.clear();
    body.clear();
    pathParams.clear();

    const auto requestLineEnd = request.find("\r\n"sv);
    if (requestLineEnd == std::string_view::npos)
    {
        return false;
    }

    std::string_view methodStr;
    std::string_view urlStr;
    std::string_view versionStr;
    if (!_ParseRequestLine(request.substr(0, requestLineEnd), methodStr, urlStr, versionStr))
    {
        return false;
    }

    method = _GetMethodFromString(methodStr);
    if (method == Methods::UNKNOWN)
    {
        return false;
    }

    if (!_ValidateHttpVersion(versionStr))
    {
        return false;
    }

    url.parse(urlStr);
    httpVersion = std::string(versionStr);

    const auto headersEnd = request.find("\r\n\r\n"sv);
    if (headersEnd == std::string_view::npos)
    {
        return false;
    }

    // PARSE HEADERS
    std::size_t lineStart = requestLineEnd + 2;
    bool hasHost = false;
    bool hasContentLength = false;
    bool hasTransferEncoding = false;
    std::size_t contentLength = 0;
    while (lineStart < headersEnd)
    {
        const auto lineEnd = request.find("\r\n"sv, lineStart);
        if (lineEnd == std::string_view::npos || lineEnd > headersEnd)
        {
            return false;
        }

        const auto header = request.substr(lineStart, lineEnd - lineStart);
        const auto separator = header.find(':');
        if (separator == std::string_view::npos || separator == 0)
        {
            return false;
        }
        if (separator > 0 && (header[separator - 1] == ' ' || header[separator - 1] == '\t'))
        {
            return false;
        }

        const auto name = header.substr(0, separator);
        const auto rawValue = header.substr(separator + 1);
        const auto value = _Trim(rawValue);

        if (!_ValidateHeaderName(name) || !_ValidateHeaderValue(rawValue))
        {
            return false;
        }

        if (_IEquals(name, "Host"))
        {
            hasHost = !value.empty();
        }
        else if (_IEquals(name, "Content-Length"))
        {
            if (hasContentLength || !_ParseContentLength(value, contentLength))
            {
                return false;
            }
            hasContentLength = true;
        }
        else if (_IEquals(name, "Transfer-Encoding"))
        {
            hasTransferEncoding = true;
        }

        headers.emplace(std::string(name), std::string(value));
        lineStart = lineEnd + 2;
    }

    if (httpVersion == "HTTP/1.1" && !hasHost)
    {
        return false;
    }
    if (hasTransferEncoding)
    {
        return false;
    }

    // PARSE BODY
    const auto bodyStart = headersEnd + 4;
    if (bodyStart < request.size())
    {
        body = std::string(request.substr(bodyStart));
    }
    if (hasContentLength && body.size() != contentLength)
    {
        return false;
    }

    return true;
}

namespace
{
HttpRequest::Methods _GetMethodFromString(std::string_view methodStr)
{
    const auto method = _ToUpper(methodStr);
    if (method == "GET")
    {
        return HttpRequest::Methods::GET;
    }
    else if (method == "POST")
    {
        return HttpRequest::Methods::POST;
    }
    else if (method == "PUT")
    {
        return HttpRequest::Methods::PUT;
    }
    else if (method == "DELETE")
    {
        return HttpRequest::Methods::DEL;
    }
    else if (method == "PATCH")
    {
        return HttpRequest::Methods::PATCH;
    }
    else if (method == "HEAD")
    {
        return HttpRequest::Methods::HEAD;
    }
    else if (method == "OPTIONS")
    {
        return HttpRequest::Methods::OPTIONS;
    }

    return HttpRequest::Methods::UNKNOWN;
}

URL::Protocols _GetProtocolFromString(std::string_view protocoldStr)
{
    const auto protocol = _ToUpper(protocoldStr);
    if (protocol == "HTTP")
    {
        return URL::Protocols::HTTP;
    }
    else if (protocol == "HTTPS")
    {
        return URL::Protocols::HTTPS;
    }

    return URL::Protocols::UNKNOWN;
}

std::string _UrlDecode(std::string_view encoded)
{
    std::string result;
    result.reserve(encoded.size());

    for (size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] == '%' && i + 2 < encoded.size())
        {
            int value;
            std::string_view hex = encoded.substr(i + 1, 2);
            if (std::from_chars(hex.data(), hex.data() + 2, value, 16).ec == std::errc{})
            {
                result += static_cast<char>(value);
                i += 2;
            }
            else
            {
                result += encoded[i];
            }
        }
        else if (encoded[i] == '+')
        {
            result += ' ';
        }
        else
        {
            result += encoded[i];
        }
    }

    return result;
}

bool _ParseRequestLine(std::string_view requestLine, std::string_view& method, std::string_view& target,
                       std::string_view& version)
{
    if (requestLine.empty())
    {
        return false;
    }

    for (const unsigned char c : requestLine)
    {
        if (c <= 31 || c == 127)
        {
            return false;
        }
    }

    const auto firstSpace = requestLine.find(' ');
    if (firstSpace == std::string_view::npos || firstSpace == 0)
    {
        return false;
    }

    const auto secondSpace = requestLine.find(' ', firstSpace + 1);
    if (secondSpace == std::string_view::npos || secondSpace == firstSpace + 1)
    {
        return false;
    }

    if (requestLine.find(' ', secondSpace + 1) != std::string_view::npos || secondSpace + 1 == requestLine.size())
    {
        return false;
    }

    method = requestLine.substr(0, firstSpace);
    target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    version = requestLine.substr(secondSpace + 1);
    return true;
}

bool _ValidateHttpVersion(std::string_view version)
{
    return version == "HTTP/1.1" || version == "HTTP/1.0";
}

bool _ValidateHeaderName(std::string_view name)
{
    if (name.empty())
    {
        return false;
    }

    for (const unsigned char c : name)
    {
        const bool isTokenChar = std::isalnum(c) || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
                                 c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' ||
                                 c == '_' || c == '`' || c == '|' || c == '~';
        if (!isTokenChar)
        {
            return false;
        }
    }

    return true;
}

bool _ValidateHeaderValue(std::string_view value)
{
    for (const unsigned char c : value)
    {
        if ((c < 32 && c != '\t') || c == 127)
        {
            return false;
        }
    }
    return true;
}

bool _ParseContentLength(std::string_view value, std::size_t& contentLength)
{
    if (value.empty())
    {
        return false;
    }

    for (const unsigned char c : value)
    {
        if (!std::isdigit(c))
        {
            return false;
        }
    }

    const auto begin = value.data();
    const auto end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, contentLength);
    return ec == std::errc{} && ptr == end;
}

bool _IEquals(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i])))
        {
            return false;
        }
    }
    return true;
}

std::string _Trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = value.find_last_not_of(" \t");
    return std::string(value.substr(first, last - first + 1));
}

std::string _ToUpper(std::string_view value)
{
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

} // namespace

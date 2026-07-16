#pragma once

#include <string_view>
#include <unordered_set>
#include <iostream>
#include <HttpRequest.hpp>

class RouteRegistry
{
public:
    static RouteRegistry& instance()
    {
        static RouteRegistry inst;
        return inst;
    }
    
    bool addRoute(std::string_view path, HttpRequest::Methods method)
    {
        auto key = makeKey(path, method);
        if (m_routes.contains(key))
        {
            std::cerr << "[ERROR] Duplicate route detected: " << path 
                      << " (method: " << methodToString(method) << ")\n";
            return false;
        }
        m_routes.insert(key);
        return true;
    }
    
    bool routeExists(std::string_view path, HttpRequest::Methods method) const
    {
        return m_routes.contains(makeKey(path, method));
    }
    
    void clear()
    {
        m_routes.clear();
    }
    
    std::size_t count() const
    {
        return m_routes.size();
    }

private:
    RouteRegistry() = default;
    
    static std::string makeKey(std::string_view path, HttpRequest::Methods method)
    {
        return std::string(path) + "_" + std::to_string(static_cast<int>(method));
    }
    
    static const char* methodToString(HttpRequest::Methods method)
    {
        switch (method)
        {
            case HttpRequest::GET:    return "GET";
            case HttpRequest::POST:   return "POST";
            case HttpRequest::PUT:    return "PUT";
            case HttpRequest::DELETE: return "DELETE";
            case HttpRequest::PATCH:  return "PATCH";
            case HttpRequest::HEAD:   return "HEAD";
            case HttpRequest::OPTIONS: return "OPTIONS";
            default:                  return "UNKNOWN";
        }
    }
    
    std::unordered_set<std::string> m_routes;
};

#define CHECK_ROUTE(path, method) \
    do { \
        if (!RouteRegistry::instance().addRoute(path, method)) { \
            throw std::runtime_error(std::string("Duplicate route: ") + path); \
        } \
    } while(0)

namespace http_method
{
    constexpr HttpRequest::Methods GET = HttpRequest::GET;
    constexpr HttpRequest::Methods POST = HttpRequest::POST;
    constexpr HttpRequest::Methods PUT = HttpRequest::PUT;
    constexpr HttpRequest::Methods DELETE = HttpRequest::DELETE;
    constexpr HttpRequest::Methods PATCH = HttpRequest::PATCH;
    constexpr HttpRequest::Methods HEAD = HttpRequest::HEAD;
    constexpr HttpRequest::Methods OPTIONS = HttpRequest::OPTIONS;
}

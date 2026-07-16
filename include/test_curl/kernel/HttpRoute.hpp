#pragma once
#include <algorithm>
#include <functional>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <test_curl/kernel/HttpRequest.hpp>
#include <test_curl/kernel/HttpResponse.hpp>
#include <vector>

struct HttpRoute
{
    enum class MatchResult
    {
        NoMatch,
        Matched,
        MethodNotAllowed
    };

    // return true if the route succeed
    template <typename Range> MatchResult operator()(Range currentRoute, HttpRequest& request, HttpResponse& response);

    std::string route;
    std::vector<HttpRequest::Methods> allowedMethods;
    std::optional<std::function<bool(const HttpRequest& request, HttpResponse& response)>> callable;
    std::vector<HttpRoute> subRoutes;
    std::optional<std::string> description;

    // Computed one time
    std::size_t nbSlashes = std::string::npos;
    std::vector<std::string> paramNames;
    std::regex paramRegex;
    bool hasParams = false;

    void parseParams();
    std::string allowedMethodsHeader() const;
};

template <typename Range>
inline HttpRoute::MatchResult HttpRoute::operator()(Range currentRoute_, HttpRequest& request, HttpResponse& response)
{
    // https://stackoverflow.com/questions/61867635/recursive-application-of-c20-range-adaptor-causes-a-compile-time-infinite-loop#:~:text=By%20creating%20span%2C%20we%20make%20the%20typename%20of%20the%20variable%20simply%20span%20instead%20of%20deeply%20nested%20typenames%20as%20shown%20in%20the%20accepted%20answer.
    auto currentRoute = std::ranges::subrange(currentRoute_);
    if (nbSlashes == std::string::npos)
    {
        nbSlashes = std::count(route.begin(), route.end(), '/');
    }

    // Handle empty route (root path) - check if currentRoute is empty
    if (std::ranges::empty(currentRoute))
    {
        // Empty route "" matches root path "/"
        if (route.empty())
        {
            response.reset();

            if (!allowedMethods.empty() &&
                std::find(allowedMethods.begin(), allowedMethods.end(), request.method) == allowedMethods.end())
            {
                response.code = 405;
                response.headers["Allow"] = allowedMethodsHeader();
                return MatchResult::MethodNotAllowed;
            }

            if (callable && response.empty())
            {
                return callable.value()(request, response) ? MatchResult::Matched : MatchResult::NoMatch;
            }
        }
        return MatchResult::NoMatch;
    }

    // Check if route has parameters
    bool routeHasParams = !route.empty() && route.find('{') != std::string_view::npos;
    std::string routeJoin;

    if (routeHasParams)
    {
        parseParams();

        // Join enough segments to match the route
        std::size_t segmentsToJoin = nbSlashes + 1;
        if (std::ranges::distance(currentRoute) >= static_cast<long>(segmentsToJoin))
        {
            auto joinedRoute = currentRoute | std::views::take(segmentsToJoin) | std::views::join_with('/');
            for (char c : joinedRoute)
            {
                routeJoin.push_back(c);
            }
        }
        else
        {
            auto front = *currentRoute.begin();
            routeJoin = std::string(std::ranges::begin(front), std::ranges::end(front));
        }

        // Try to match
        std::smatch match;
        std::string matchStr(routeJoin);
        if (!std::regex_match(matchStr, match, paramRegex))
        {
            return MatchResult::NoMatch;
        }

        // Extract parameters into request
        for (size_t i = 0; i < paramNames.size(); ++i)
        {
            request.pathParams[paramNames[i]] = match[i + 1].str();
        }
    }
    else
    {
        auto front = *currentRoute.begin();
        routeJoin = std::string(std::ranges::begin(front), std::ranges::end(front));
        if (nbSlashes > 1)
        {
            auto joinedRoute = currentRoute | std::views::take(nbSlashes + 1) | std::views::join_with('/');
            routeJoin.clear();
            for (char c : joinedRoute)
            {
                routeJoin.push_back(c);
            }
        }

        if (routeJoin != route)
        {
            return MatchResult::NoMatch;
        }
    }

    response.reset();

    if (!allowedMethods.empty() &&
        std::find(allowedMethods.begin(), allowedMethods.end(), request.method) == allowedMethods.end())
    {
        response.code = 405;
        response.headers["Allow"] = allowedMethodsHeader();
        return MatchResult::MethodNotAllowed;
    }

    auto routeToCover = currentRoute | std::views::drop(nbSlashes + 1);

    for (auto& subRoute : subRoutes)
    {
        auto subRouteResult = subRoute(routeToCover, request, response);
        if (subRouteResult != MatchResult::NoMatch)
        {
            return subRouteResult;
        }
    }

    if (callable && response.empty())
    {
        return callable.value()(request, response) ? MatchResult::Matched : MatchResult::NoMatch;
    }

    return MatchResult::NoMatch;
}

inline void HttpRoute::parseParams()
{
    if (hasParams)
        return;

    hasParams = true;

    std::smatch match;
    std::regex paramPattern(R"(\{([^}]+)\})");
    std::string routeStr(route);
    std::string searchStr = routeStr;

    while (std::regex_search(searchStr, match, paramPattern))
    {
        paramNames.push_back(match[1].str());
        searchStr = match.suffix().str();
    }

    if (!paramNames.empty())
    {
        std::string regexStr = routeStr;
        for (const auto& param : paramNames)
        {
            regexStr = std::regex_replace(regexStr, std::regex("\\{" + param + "\\}"), "([^/]+)");
        }
        paramRegex = std::regex(regexStr);
    }
}

inline std::string HttpRoute::allowedMethodsHeader() const
{
    auto methodToString = [](HttpRequest::Methods method) -> std::string_view
    {
        switch (method)
        {
        case HttpRequest::GET:
            return "GET";
        case HttpRequest::POST:
            return "POST";
        case HttpRequest::PUT:
            return "PUT";
        case HttpRequest::DEL:
            return "DELETE";
        case HttpRequest::PATCH:
            return "PATCH";
        case HttpRequest::HEAD:
            return "HEAD";
        case HttpRequest::OPTIONS:
            return "OPTIONS";
        default:
            return "UNKNOWN";
        }
    };

    std::string result;
    for (auto method : allowedMethods)
    {
        if (!result.empty())
        {
            result += ", ";
        }
        result += methodToString(method);
    }
    return result;
}

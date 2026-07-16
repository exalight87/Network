#pragma once
#include <string>
#include <functional>
#include <optional>
#include <vector>
#include <algorithm>
#include <ranges>
#include <regex>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>

struct HttpRoute
{
	// return true if the route succeed
	template < typename Range >
	bool operator()(Range currentRoute, HttpRequest& request, HttpResponse& response);

	std::string_view route;
	std::vector<HttpRequest::Methods> allowedMethods;
	std::optional< std::function< bool(const HttpRequest& request, HttpResponse& response) > > callable;
	std::vector<HttpRoute> subRoutes;
	std::optional<std::string> description;

	// Computed one time
	std::size_t nbSlashes = std::string::npos;
	std::vector<std::string> paramNames;
	std::regex paramRegex;
	bool hasParams = false;

	void parseParams();
};

template <typename Range>
inline bool HttpRoute::operator()(Range currentRoute_, HttpRequest& request, HttpResponse& response)
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

			if (!allowedMethods.empty() && std::find(allowedMethods.begin(), allowedMethods.end(), request.method) == allowedMethods.end())
			{
				response.code = 405;
				return false;
			}

			if (callable && response.empty())
			{
				return callable.value()(request, response);
			}
		}
		return false;
	}

	// Check if route has parameters
	bool routeHasParams = !route.empty() && route.find('{') != std::string_view::npos;
	std::string_view routeJoin;
	
	if (routeHasParams) {
		parseParams();
		
		// Join enough segments to match the route
		std::size_t segmentsToJoin = nbSlashes + 1;
		if (std::ranges::distance(currentRoute) >= static_cast<long>(segmentsToJoin)) {
			auto joinedRoute = currentRoute | std::views::take(segmentsToJoin) | std::views::join_with('/');
			routeJoin = std::string_view(&(*joinedRoute.begin()), std::ranges::distance(joinedRoute));
		} else {
			routeJoin = std::string_view(currentRoute.front());
		}
		
		// Try to match
		std::smatch match;
		std::string matchStr(routeJoin);
		if (!std::regex_match(matchStr, match, paramRegex)) {
			return false;
		}
		
		// Extract parameters into request
		for (size_t i = 0; i < paramNames.size(); ++i) {
			request.pathParams[paramNames[i]] = match[i + 1].str();
		}
	}
	else
	{
		routeJoin = std::string_view(currentRoute.front());
		if (nbSlashes > 1)
		{
			auto joinedRoute = currentRoute | std::views::take(nbSlashes + 1) | std::views::join_with('/');
			routeJoin = std::string_view(&(*joinedRoute.begin()), std::ranges::distance(joinedRoute));
		}

		if (routeJoin != route)
		{
			return false;
		}
	}

	response.reset();

	if (!allowedMethods.empty() && std::find(allowedMethods.begin(), allowedMethods.end(), request.method) == allowedMethods.end())
	{
		response.code = 405;
		return false;
	}

	auto routeToCover = currentRoute | std::views::drop(nbSlashes + 1);

	for (auto& subRoute : subRoutes)
	{
		if( subRoute(routeToCover, request, response) )
		{
			return true;
		}
	}

	if ( callable && response.empty() )
	{
		return callable.value()(request, response);
	}

	return false;
}

inline void HttpRoute::parseParams()
{
	if (hasParams) return;
	
	hasParams = true;
	
	std::smatch match;
	std::regex paramPattern(R"(\{([^}]+)\})");
	std::string routeStr(route);
	std::string searchStr = routeStr;
	
	while (std::regex_search(searchStr, match, paramPattern)) {
		paramNames.push_back(match[1].str());
		searchStr = match.suffix().str();
	}
	
	if (!paramNames.empty()) {
		std::string regexStr = routeStr;
		for (const auto& param : paramNames) {
			regexStr = std::regex_replace(regexStr, std::regex("\\{" + param + "\\}"), "([^/]+)");
		}
		paramRegex = std::regex(regexStr);
	}
}
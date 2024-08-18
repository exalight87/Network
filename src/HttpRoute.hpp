#pragma once
#include <string>
#include <functional>
#include <optional>
#include <HttpRequest.hpp>
#include <HttpResponse.hpp>

struct HttpRoute
{
	// return true if the route succeed
	template < typename Range >
	bool operator()(Range currentRoute, const HttpRequest& request, HttpResponse& response);

	std::string_view route;
	std::vector<HttpRequest::Methods> allowedMethods;
	std::optional< std::function< bool(const HttpRequest& request, HttpResponse& response) > > callable;
	std::vector<HttpRoute> subRoutes;

	// Computed one time
	std::size_t nbSlashes = std::string::npos;
};

template <typename Range>
inline bool HttpRoute::operator()(Range currentRoute_, const HttpRequest& request, HttpResponse& response)
{
	// https://stackoverflow.com/questions/61867635/recursive-application-of-c20-range-adaptor-causes-a-compile-time-infinite-loop#:~:text=By%20creating%20span%2C%20we%20make%20the%20typename%20of%20the%20variable%20simply%20span%20instead%20of%20deeply%20nested%20typenames%20as%20shown%20in%20the%20accepted%20answer.
	auto currentRoute = std::ranges::subrange(currentRoute_);
	if (nbSlashes == std::string::npos)
	{
		nbSlashes = std::ranges::count(route, '/');
	}

	std::string_view routeJoin = std::string_view(currentRoute.front());
	if (nbSlashes > 1)
	{
		auto joinedRoute = currentRoute | std::views::take(nbSlashes + 1) | std::views::join_with('/');
		routeJoin = std::string_view(&(*joinedRoute.begin()), std::ranges::distance(joinedRoute));
	}

	if (routeJoin != route)
	{
		return false;
	}

	response.reset();

	if (!allowedMethods.empty() && std::find(allowedMethods.begin(), allowedMethods.end(), request.method) == allowedMethods.end())
	{
		response.code = 401;
		return false;
	}

	auto routeToCover = currentRoute | std::views::drop(nbSlashes + 1);

	for (auto& route : subRoutes)
	{
		if( route(routeToCover, request, response) )
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
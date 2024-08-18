#include <HttpRequest.hpp>
#include <iostream>
#include <format>
#include <string_view>

namespace
{
	HttpRequest::Methods _GetMethodFromString(std::string_view methodStr);
	URL::Protocols _GetProtocolFromString(std::string_view protocoldStr);
}

void URL::parse(std::string_view url)
{
	// absolute uri
	if (!url.starts_with('/'))
	{
		// PROTOCOL
		std::string_view domainDelim = "://";
		auto protocolSize = url.find_first_of(domainDelim);
		if (protocolSize == std::string::npos)
		{
			// TODO handle no protocol
		}

		protocol = _GetProtocolFromString( url.substr(0, protocolSize) );
		url.remove_prefix(protocolSize + domainDelim.size());

		// DOMAIN
		std::string_view pathDelim = "/";
		auto domainSize = url.find_first_of(pathDelim);
		if (domainSize == std::string::npos)
		{
			// TODO handle no domain
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
	url.remove_prefix(pathSize + (isQueryArgs ? queryArgsDelim.size() : 0));

	// QUERRY PARAMS
	for (auto queryArg : std::views::split(url, '&') | std::ranges::views::transform([](auto&& rng) {
		return std::string_view(&*rng.begin(), std::ranges::distance(rng));
		}))
	{
		auto splitedQueryArg = std::views::split(queryArg, '=') | std::ranges::views::transform([](auto&& rng) {
				return std::string(&*rng.begin(), std::ranges::distance(rng));
			});
		auto splitedQueryArgIt = splitedQueryArg.begin();

		std::pair<std::string, std::string> paramsPair = std::make_pair(*splitedQueryArgIt, "");

		// handle query params without value
		if (!(*++splitedQueryArgIt).empty())
		{
			paramsPair.second = *splitedQueryArgIt;
		}

		queryParams.insert(paramsPair);
	}

}

bool HttpRequest::parse(std::string_view request)
{
	using namespace std::literals;
	auto reqByLine = std::views::split(request, "\r\n"sv);

	// PARSE REQUEST-LINE
	auto requestLine = reqByLine.front() | std::views::split(' ') | std::ranges::views::transform([](auto&& rng) {
		return std::string_view(&*rng.begin(), std::ranges::distance(rng));
		});
	auto requestLineIt = requestLine.begin();

	method = _GetMethodFromString( *requestLineIt++ );
	if (method == Methods::UNKNOWN)
	{
		return false;
	}

	url.parse(*requestLineIt++);
	httpVersion = std::string(*requestLineIt);

	// PARSE HEADERS
	for (const auto& header : reqByLine | std::views::drop(1) | std::ranges::views::transform([](auto&& rng) {
		return std::string_view(&*rng.begin(), std::ranges::distance(rng));
		}))
	{
		if (header.empty() || header == "\r\n"sv)
		{
			continue;
		}

		auto headerArg = std::views::split(header, ':') | std::ranges::views::transform([](auto&& rng) {
			return std::string_view(&*rng.begin(), std::ranges::distance(rng));
			});
		auto headerIt = headerArg.begin();

		std::pair<std::string, std::string> headerPair = std::make_pair(std::string( *headerIt ), "");

		// handle query params without value
		if (!(*++headerIt).empty())
		{
			// trim value
			std::string_view value = *headerIt;
			value.remove_prefix(std::min((*headerIt).find_first_not_of(" "), (*headerIt).size()));
			headerPair.second = std::string(value);
		}

		headers.insert(headerPair);
	}

	return true;
}

namespace
{
	HttpRequest::Methods _GetMethodFromString(std::string_view methodStr)
	{
		if (methodStr == "GET")
		{
			return HttpRequest::Methods::GET;
		}
		else if (methodStr == "POST")
		{
			return HttpRequest::Methods::POST;
		}
		else if (methodStr == "PUT")
		{
			return HttpRequest::Methods::PUT;
		}

		return HttpRequest::Methods::UNKNOWN;
	}

	URL::Protocols _GetProtocolFromString(std::string_view protocoldStr)
	{
		if (protocoldStr == "HTTP")
		{
			return URL::Protocols::HTTP;
		}
		else if (protocoldStr == "HTTPS")
		{
			return URL::Protocols::HTTPS;
		}

		return URL::Protocols::UNKNOWN;
	}
}
#include <HttpRequest.hpp>
#include <iostream>
#include <format>
#include <string_view>
#include <unordered_map>
#include <ranges>

namespace
{
	HttpRequest::Methods _GetMethodFromString(std::string_view methodStr);
	URL::Protocols _GetProtocolFromString(std::string_view protocoldStr);
	std::string _UrlDecode(std::string_view encoded);
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

		std::pair<std::string, std::string> paramsPair = std::make_pair(_UrlDecode(*splitedQueryArgIt), "");

		// handle query params without value
		if (!(*++splitedQueryArgIt).empty())
		{
			paramsPair.second = _UrlDecode(*splitedQueryArgIt);
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
	size_t headerCount = 0;
	for (const auto& header : reqByLine | std::views::drop(1) | std::ranges::views::transform([](auto&& rng) {
		return std::string_view(&*rng.begin(), std::ranges::distance(rng));
		}))
	{
		if (header.empty() || header == "\r\n"sv)
		{
			break;
		}
		headerCount++;

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

	// PARSE BODY
	auto bodyIt = reqByLine.begin();
	std::advance(bodyIt, 1 + headerCount + 1); // request line + headers + empty line
	if (bodyIt != reqByLine.end())
	{
		auto bodyView = *bodyIt | std::views::split('\0') | std::ranges::views::transform([](auto&& rng) {
			return std::string_view(&*rng.begin(), std::ranges::distance(rng));
		});
		auto bodyIt2 = bodyView.begin();
		if (bodyIt2 != bodyView.end() && !(*bodyIt2).empty())
		{
			body = std::string(*bodyIt2);
		}
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
		else if (methodStr == "DELETE")
		{
			return HttpRequest::Methods::DELETE;
		}
		else if (methodStr == "PATCH")
		{
			return HttpRequest::Methods::PATCH;
		}
		else if (methodStr == "HEAD")
		{
			return HttpRequest::Methods::HEAD;
		}
		else if (methodStr == "OPTIONS")
		{
			return HttpRequest::Methods::OPTIONS;
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

}
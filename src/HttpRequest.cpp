#include <HttpRequest.hpp>
#include <iostream>
#include <format>
#include <string_view>
#include <unordered_map>
#include <ranges>
#include <charconv>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace
{
	HttpRequest::Methods _GetMethodFromString(std::string_view methodStr);
	URL::Protocols _GetProtocolFromString(std::string_view protocoldStr);
	std::string _UrlDecode(std::string_view encoded);
	std::string _Trim(std::string_view value);
	std::string _ToUpper(std::string_view value);
}

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

		protocol = _GetProtocolFromString( url.substr(0, protocolSize) );
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

	std::string requestLine(request.substr(0, requestLineEnd));
	std::istringstream requestLineStream(requestLine);
	std::string methodStr;
	std::string urlStr;
	std::string versionStr;

	if (!(requestLineStream >> methodStr >> urlStr >> versionStr))
	{
		return false;
	}

	method = _GetMethodFromString(methodStr);
	if (method == Methods::UNKNOWN)
	{
		return false;
	}

	url.parse(urlStr);
	httpVersion = versionStr;

	const auto headersEnd = request.find("\r\n\r\n"sv);
	if (headersEnd == std::string_view::npos)
	{
		return false;
	}

	// PARSE HEADERS
	std::size_t lineStart = requestLineEnd + 2;
	while (lineStart < headersEnd)
	{
		const auto lineEnd = request.find("\r\n"sv, lineStart);
		if (lineEnd == std::string_view::npos || lineEnd > headersEnd)
		{
			return false;
		}

		const auto header = request.substr(lineStart, lineEnd - lineStart);
		const auto separator = header.find(':');
		if (separator == std::string_view::npos)
		{
			return false;
		}

		headers.emplace(std::string(header.substr(0, separator)), _Trim(header.substr(separator + 1)));
		lineStart = lineEnd + 2;
	}

	// PARSE BODY
	const auto bodyStart = headersEnd + 4;
	if (bodyStart < request.size())
	{
		body = std::string(request.substr(bodyStart));
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
			return HttpRequest::Methods::DELETE;
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
		std::ranges::transform(result, result.begin(), [](unsigned char c) {
			return static_cast<char>(std::toupper(c));
		});
		return result;
	}

}

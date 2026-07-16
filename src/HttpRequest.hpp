#pragma once
#include <string>
#include <unordered_map>

struct URL
{
	enum Protocols
	{
		UNKNOWN,
		HTTP,
		HTTPS
	};

	void parse(std::string_view url);

	Protocols protocol = Protocols::UNKNOWN;
	std::string domain;
	std::string path;
	std::unordered_map<std::string, std::string> queryParams;
};


struct HttpRequest
{
	enum Methods
	{
		UNKNOWN,
		GET,
		POST,
		PUT,
		DELETE,
		PATCH,
		HEAD,
		OPTIONS
	};

	Methods method = Methods::UNKNOWN;
	URL url;
	std::string httpVersion;

	std::unordered_map<std::string, std::string> headers;
	std::string body;

	bool parse(std::string_view request);
};
#pragma once
#include <string>
#include <map>

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
	std::map<std::string, std::string> queryParams;
};


struct HttpRequest
{
	enum Methods
	{
		UNKNOWN,
		GET,
		PUT,
		POST
	};

	Methods method = Methods::UNKNOWN;
	URL url;
	std::string httpVersion;

	std::map<std::string, std::string> headers;
	std::string body;

	bool parse(std::string_view request);
};
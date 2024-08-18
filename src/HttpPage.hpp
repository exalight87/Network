#pragma once
#include <string>
#include <optional>

class HttpPage
{
public:
	HttpPage() = default;
	HttpPage(std::string title, std::string body) : m_title(title), m_body(body) {};
	std::string str() const;
	void addMeta(std::string&& meta);
	void addLink(std::string&& link);
	void setBody(std::string&& body);
	void setTitle(std::string&& title);
private:
	std::optional<std::string> m_title;
	std::vector<std::string> m_metas;
	std::vector<std::string> m_links;
	std::string m_body;
};
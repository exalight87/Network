#include "HttpPage.hpp"
#include <sstream>

std::string HttpPage::str() const
{
    std::ostringstream page;
    page << "<html lang=\"en\">" << '\n';

    page << "</head>" << '\n';
        for (auto&& link : m_links)
        {
            page << link << '\n';
        }
        for (auto&& meta : m_metas)
        {
            page << meta << '\n';
        }
        page << "<meta charset=\"ISO-8859-1\">" << '\n';
        page << "<meta name = \"viewport\" content = \"width=device-width, initial-scale=1.0\">" << '\n';
        page << "<title>" << m_title.value_or("") << "</title>" << '\n';
    page << "</head>" << '\n';

    page << "</body>" << '\n';
        page << m_body;
    page << "</body>" << '\n';

    page << "</html>" << '\n';
    return page.str();
}

void HttpPage::addMeta(std::string&& meta)
{
    m_metas.push_back(meta);
}

void HttpPage::addLink(std::string&& link)
{
    m_links.push_back(link);
}

void HttpPage::setBody(std::string&& body)
{
    m_body = body;
}

void HttpPage::setTitle(std::string&& title)
{
    m_title = title;
}

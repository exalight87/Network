#pragma once
#include <string>
#include <test_curl/kernel/HttpRequest.hpp>
#include <test_curl/kernel/HttpRoute.hpp>
#include <vector>

class ApiDocs
{
  public:
    struct DocEndpoint
    {
        std::string path;
        std::string methods;
        std::string description;
    };

    static std::string generateHtml(const std::vector<HttpRoute>& routes, uint16_t port)
    {
        std::vector<DocEndpoint> endpoints;
        collectRoutes(routes, "", endpoints);

        std::string portStr = std::to_string(port);
        std::string html = "<!DOCTYPE html>\n"
                           "<html lang=\"en\">\n"
                           "<head>\n"
                           "    <meta charset=\"UTF-8\">\n"
                           "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
                           "    <title>API Documentation</title>\n"
                           "    <style>\n"
                           "        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, "
                           "sans-serif; max-width: 900px; margin: 0 auto; padding: 20px; background: #f5f5f5; }\n"
                           "        h1 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 10px; }\n"
                           "        .endpoint { background: white; border-radius: 8px; padding: 16px; margin-bottom: "
                           "12px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }\n"
                           "        .path { font-size: 18px; font-weight: 600; color: #007bff; }\n"
                           "        .method { display: inline-block; padding: 4px 8px; border-radius: 4px; font-size: "
                           "12px; font-weight: bold; margin-left: 8px; }\n"
                           "        .GET { background: #28a745; color: white; }\n"
                           "        .POST { background: #007bff; color: white; }\n"
                           "        .PUT { background: #ffc107; color: #333; }\n"
                           "        .DELETE { background: #dc3545; color: white; }\n"
                           "        .PATCH { background: #6c757d; color: white; }\n"
                           "        .description { margin-top: 8px; color: #666; }\n"
                           "        .port { color: #666; font-size: 14px; }\n"
                           "    </style>\n"
                           "</head>\n"
                           "<body>\n"
                           "    <h1>API Documentation <span class=\"port\">(Port: " +
                           portStr + ")</span></h1>\n";

        if (endpoints.empty())
        {
            html += "<p>No API endpoints documented.</p>";
        }
        else
        {
            for (const auto& ep : endpoints)
            {
                html += "    <div class=\"endpoint\">\n";
                html += "        <span class=\"path\">" + ep.path + "</span>\n";

                std::string methodsHtml;
                std::string methodList[] = {"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
                for (char m : ep.methods)
                {
                    for (const auto& known : methodList)
                    {
                        if (known[0] == m)
                        {
                            methodsHtml += "<span class=\"method " + known + "\">" + known + "</span>";
                            break;
                        }
                    }
                }
                html += methodsHtml + "\n";

                if (!ep.description.empty())
                {
                    html += "        <div class=\"description\">" + ep.description + "</div>\n";
                }
                html += "    </div>\n";
            }
        }

        html += "</body>\n</html>";

        return html;
    }

  private:
    static void collectRoutes(const std::vector<HttpRoute>& routes, const std::string& prefix,
                              std::vector<DocEndpoint>& endpoints)
    {
        for (const auto& route : routes)
        {
            std::string fullPath = prefix.empty() ? std::string(route.route) : prefix + "/" + std::string(route.route);

            if (route.route.empty())
            {
                fullPath = prefix.empty() ? "/" : prefix;
            }

            bool hasHandler = route.callable.has_value();
            bool hasSubRoutes = !route.subRoutes.empty();

            if (hasHandler || hasSubRoutes)
            {
                std::string methodsStr;
                if (!route.allowedMethods.empty())
                {
                    for (size_t i = 0; i < route.allowedMethods.size(); ++i)
                    {
                        methodsStr += methodToChar(route.allowedMethods[i]);
                        if (i < route.allowedMethods.size() - 1)
                            methodsStr += ", ";
                    }
                }
                else if (hasHandler)
                {
                    methodsStr = "GET";
                }

                std::string desc;
                if (route.description.has_value())
                {
                    desc = *route.description;
                }

                if (hasHandler)
                {
                    endpoints.push_back({fullPath, methodsStr, desc});
                }

                if (hasSubRoutes)
                {
                    collectRoutes(route.subRoutes, fullPath, endpoints);
                }
            }
        }
    }

    static char methodToChar(HttpRequest::Methods method)
    {
        switch (method)
        {
        case HttpRequest::GET:
            return 'G';
        case HttpRequest::POST:
            return 'P';
        case HttpRequest::PUT:
            return 'U';
        case HttpRequest::DEL:
            return 'D';
        case HttpRequest::PATCH:
            return 'T';
        case HttpRequest::HEAD:
            return 'H';
        case HttpRequest::OPTIONS:
            return 'O';
        default:
            return '?';
        }
    }
};

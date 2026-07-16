#include <HttpResponse.hpp>
#include <format>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace {
    std::string_view _GetContentType(std::string_view filename);
    std::string ROOT_FOLDER = []() {
        const char* envPath = std::getenv("SERVER_ROOT");
        if (envPath)
        {
            return std::string(envPath);
        }
#ifdef _WIN32
        return std::string("E:/projet/test_curl");
#else
        return std::string(".");
#endif
    }();
}

HttpResponse HttpResponse::CODE_404 = {
	.headers{
		{"Content-Type", "text/html; charset=ISO-8859-1"}
	},
    .code = 404,
    .body = HttpPage{
        "404 page",
        "<h1>C'est cass� !</h1>"
    }
};

HttpResponse HttpResponse::CLOSE_CONNECTION = {
    .headers = {
        {"Content-Type", "text/html; charset=ISO-8859-1"},
        {"Connection", "close"}
    },
    .code = 200,
};

HttpResponse HttpResponse::OPEN_CONNECTION = {
    .headers = {
        {"Content-Type", "text/html; charset=ISO-8859-1"},
    },
    .code = 200,
    .body = "OK"
};

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>; // line not needed in C++20...

std::string HttpResponse::format()  const
{
    std::string pageStr;
    std::visit(overload{
         [&pageStr](const std::string& str) { pageStr = str; },
         [&pageStr](const HttpPage& page) { pageStr = page.str(); }
    }, body);

    size_t bodySize = pageStr.size();
    size_t headerSize = 20;
    for (const auto& [key, value] : headers)
    {
        headerSize += key.size() + value.size() + 4;
    }

    std::string response;
    response.reserve(headerSize + bodySize + 64);

    response = std::format("HTTP/1.1 {}\r\n", code);

    for (const auto& [key, value] : headers)
    {
        response += std::format("{}: {}\r\n", key, value);
    }

    response += std::format("Content-Length: {}\r\n\r\n", bodySize);
    response += pageStr;

    return response;
}

bool HttpResponse::loadFile(const std::string& filename)
{
    std::ifstream f(ROOT_FOLDER + '/' + filename, std::ios::binary);
    if (!f) {
        return false;
    }

    headers["Content-Type"] = _GetContentType( filename );

    std::ostringstream oss;
    oss << f.rdbuf();
    body = oss.str();

    return true;
}

namespace {
    std::string_view _GetContentType(std::string_view filename)
    {
        std::string_view ext = filename.substr(filename.find_last_of('.') + 1);

        if (ext == "html")
        {
            return "text/html";
        }
        else if (ext == "css")
        {
            return "text/css";
        }
        else if (ext == "ico")
        {
            return "image/x-icon";
        }
        else if (ext == "jpeg" || ext == "jpg")
        {
            return "image/jpeg";
        }
        else if (ext == "json")
        {
            return "application/json";
        }
        else if (ext == "js")
        {
            return "application/javascript";
        }
        else if (ext == "png")
        {
            return "image/png";
        }
        else if (ext == "webp")
        {
            return "image/webp";
        }

        return "application/octet-stream";
    }
}
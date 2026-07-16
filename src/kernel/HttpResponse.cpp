#include <test_curl/kernel/HttpResponse.hpp>
#include <format>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>

namespace {
    std::string_view _GetContentType(std::string_view filename);
    std::string_view _ReasonPhrase(uint32_t code);
    bool _LoadFileUnder(HttpResponse& response, const std::filesystem::path& baseDir, const std::filesystem::path& filename);
    std::filesystem::path _RootFolder()
    {
        const char* envPath = std::getenv("SERVER_ROOT");
        if (envPath)
        {
            return envPath;
        }
        return std::filesystem::current_path();
    }
    bool _IsPathInside(const std::filesystem::path& root, const std::filesystem::path& path);
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
    .body = "",
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

std::string HttpResponse::format(bool includeBody) const
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

    response = std::format("HTTP/1.1 {} {}\r\n", code, _ReasonPhrase(code));

    for (const auto& [key, value] : headers)
    {
        response += std::format("{}: {}\r\n", key, value);
    }

    response += std::format("Content-Length: {}\r\n\r\n", bodySize);
    if (includeBody)
    {
        response += pageStr;
    }

    return response;
}

bool HttpResponse::loadFile(const std::string& filename)
{
    return _LoadFileUnder(*this, ".", filename);
}

bool HttpResponse::loadFileFrom(const std::string& baseDir, const std::string& filename)
{
    return _LoadFileUnder(*this, baseDir, filename);
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

    bool _LoadFileUnder(HttpResponse& response, const std::filesystem::path& baseDir, const std::filesystem::path& filename)
    {
        namespace fs = std::filesystem;

        std::error_code error;
        const auto serverRoot = fs::weakly_canonical(_RootFolder(), error);
        if (error)
        {
            return false;
        }

        const auto root = fs::weakly_canonical(serverRoot / baseDir, error);
        if (error || !_IsPathInside(serverRoot, root))
        {
            return false;
        }

        fs::path requestedPath(filename);
        if (requestedPath.is_absolute())
        {
            requestedPath = requestedPath.relative_path();
        }

        const auto finalPath = fs::weakly_canonical(root / requestedPath.lexically_normal(), error);
        if (error || !_IsPathInside(root, finalPath))
        {
            return false;
        }

        std::ifstream f(finalPath, std::ios::binary);
        if (!f) {
            return false;
        }

        response.headers["Content-Type"] = _GetContentType(finalPath.string());

        std::ostringstream oss;
        oss << f.rdbuf();
        response.body = oss.str();

        return true;
    }

    bool _IsPathInside(const std::filesystem::path& root, const std::filesystem::path& path)
    {
        auto rootIt = root.begin();
        auto pathIt = path.begin();

        for (; rootIt != root.end(); ++rootIt, ++pathIt)
        {
            if (pathIt == path.end() || *rootIt != *pathIt)
            {
                return false;
            }
        }

        return true;
    }

    std::string_view _ReasonPhrase(uint32_t code)
    {
        switch (code)
        {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 431: return "Request Header Fields Too Large";
        case 501: return "Not Implemented";
        case 505: return "HTTP Version Not Supported";
        case 500: return "Internal Server Error";
        default: return "";
        }
    }
}

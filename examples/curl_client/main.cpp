#include <test_curl/integrations/curl/NetworkCurl.hpp>

#include <format>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const std::string url = argc > 1 ? argv[1] : "http://example.com";

    auto& client = NetworkCurl::GetInstance();
    const auto response = client.Get(url);

    std::cout << std::format("{}\n", response);
    return response.code == 0 ? 1 : 0;
}

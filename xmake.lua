add_rules("mode.debug", "mode.release")
set_warnings("all", "error")
set_languages("c++23")

add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

option("with_gtest")
    set_default(false)
    set_showmenu(true)
    set_description("Enable xmake-managed GoogleTest targets")
option_end()

option("with_curl")
    set_default(false)
    set_showmenu(true)
    set_description("Enable xmake-managed libcurl integration targets")
option_end()

local gtest_enabled = has_config("with_gtest")
local curl_enabled = has_config("with_curl")

if gtest_enabled then
    add_requires("gtest")
end

if curl_enabled then
    add_requires("libcurl")
end

local kernel_headers = "include/test_curl/kernel/*.hpp"
local kernel_sources = "src/kernel/*.cpp"

local function add_project_includes()
    add_includedirs("include", {public = true})
end

local function add_platform_links()
    if is_plat("windows") then
        add_syslinks("ws2_32")
        add_defines("_CRT_SECURE_NO_WARNINGS")
    else
        add_syslinks("pthread")
    end
end

target("http_kernel")
    set_kind("static")
    add_project_includes()
    add_files(kernel_sources)
    add_headerfiles(kernel_headers, {prefixdir = "test_curl/kernel"})
    add_platform_links()

target("http_curl")
    set_kind("static")
    set_enabled(curl_enabled)
    add_project_includes()
    add_files("src/integrations/curl/*.cpp")
    add_headerfiles("include/test_curl/integrations/curl/*.hpp", {prefixdir = "test_curl/integrations/curl"})
    if curl_enabled then
        add_packages("libcurl", {public = true})
    end
    add_platform_links()

target("http_server")
    set_kind("binary")
    add_project_includes()
    add_files("examples/showcase_server/main.cpp")
    add_deps("http_kernel")
    add_platform_links()

target("core_unit_tests")
    set_kind("binary")
    add_project_includes()
    add_files("tests/core_unit_test.cpp")
    add_deps("http_kernel")
    add_platform_links()
    add_tests("core_unit_tests")

local function add_gtest_target(name, source, deps, enabled)
    target(name)
        set_kind("binary")
        set_enabled(enabled)
        add_project_includes()
        add_includedirs("tests")
        add_files(source)
        for _, dep in ipairs(deps) do
            add_deps(dep)
        end
        if gtest_enabled then
            add_packages("gtest")
        end
        add_platform_links()
        add_tests(name)
end

add_gtest_target("route_duplicate_tests", "tests/route_duplicate_test.cpp", {"http_kernel"}, gtest_enabled)
add_gtest_target("server_tests", "tests/server_test.cpp", {"http_kernel", "http_curl"}, gtest_enabled and curl_enabled)
add_gtest_target("performance_tests", "tests/performance_test.cpp", {"http_kernel", "http_curl"}, gtest_enabled and curl_enabled)
add_gtest_target("robustness_tests", "tests/robustness_test.cpp", {"http_kernel", "http_curl"}, gtest_enabled and curl_enabled)

local function add_example_target(name, source)
    target(name)
        set_kind("binary")
        add_project_includes()
        add_files(source)
        add_deps("http_kernel")
        add_platform_links()
end

add_example_target("example_simple_server", "examples/simple_server/main.cpp")
add_example_target("example_file_server", "examples/file_server/main.cpp")
add_example_target("example_api_server", "examples/api_server/main.cpp")

target("example_curl_client")
    set_kind("binary")
    set_enabled(curl_enabled)
    add_project_includes()
    add_files("examples/curl_client/main.cpp")
    add_deps("http_curl")
    add_platform_links()

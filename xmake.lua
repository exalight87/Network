add_rules("mode.debug", "mode.release")
set_warnings("all", "error")
set_languages("c++23")

-- Enable compilation database for LSP support
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

add_requires("libcurl", "gtest")

target("test_curl")
    set_kind("static")
    add_files("src/*.cpp")
    remove_files("src/main.cpp")
    add_includedirs("src")
    add_headerfiles("src/*.hpp", {prefixdir = "include"})
    add_packages("libcurl")
    add_defines("_CRT_SECURE_NO_WARNINGS")

-- Add test target
target("core_unit_tests")
    set_kind("binary")
    add_files("tests/core_unit_test.cpp")
    add_files("src/HttpRequest.cpp", "src/HttpResponse.cpp", "src/HttpPage.cpp", "src/HttpRoute.cpp")
    add_includedirs("tests")
    add_includedirs("src")

target("server_tests")
    set_kind("binary")
    add_files("tests/server_test.cpp")
    add_deps("test_curl", "http_server")
    add_packages("gtest", "libcurl")
    
    add_includedirs("tests")
    add_includedirs("src")

target("performance_tests")
    set_kind("binary")
    add_files("tests/performance_test.cpp")
    add_deps("test_curl", "http_server")
    add_packages("gtest", "libcurl")
    
    add_includedirs("tests")
    add_includedirs("src")

    if is_plat("windows") then
        add_syslinks("kernel32")
    end

target("robustness_tests")
    set_kind("binary")
    add_files("tests/robustness_test.cpp")
    add_deps("test_curl", "http_server")
    add_packages("gtest", "libcurl")
    
    add_includedirs("tests")
    add_includedirs("src")

    if is_plat("windows") then
        add_syslinks("kernel32")
    end

target("route_duplicate_tests")
    set_kind("binary")
    add_files("tests/route_duplicate_test.cpp")
    add_deps("test_curl")
    add_packages("gtest")
    
    add_includedirs("tests")
    add_includedirs("src")

    if is_plat("windows") then
        add_syslinks("kernel32")
    end

target("http_server")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("test_curl")
    add_includedirs("src")
    add_headerfiles("src/*.hpp", {prefixdir = "include"})
    add_packages("libcurl")
    if not is_plat("windows") then
        add_syslinks("pthread")
    end
    add_defines("_CRT_SECURE_NO_WARNINGS")

-- Example: Simple Server
target("example_simple_server")
    set_kind("binary")
    add_files("examples/simple_server/main.cpp")
    add_deps("test_curl")
    add_includedirs("src")
    add_packages("libcurl")
    if not is_plat("windows") then
        add_syslinks("pthread")
    end
    add_defines("_CRT_SECURE_NO_WARNINGS")

-- Example: File Server
target("example_file_server")
    set_kind("binary")
    add_files("examples/file_server/main.cpp")
    add_deps("test_curl")
    add_includedirs("src")
    add_packages("libcurl")
    if not is_plat("windows") then
        add_syslinks("pthread")
    end
    add_defines("_CRT_SECURE_NO_WARNINGS")

-- Example: API Server
target("example_api_server")
    set_kind("binary")
    add_files("examples/api_server/main.cpp")
    add_deps("test_curl")
    add_includedirs("src")
    add_packages("libcurl")
    if not is_plat("windows") then
        add_syslinks("pthread")
    end
    add_defines("_CRT_SECURE_NO_WARNINGS")

task("test")
    set_menu {
        usage = "xmake test",
        description = "Build and run all test binaries"
    }
    on_run(function ()
        local targets = {
            "core_unit_tests",
            "route_duplicate_tests",
            "server_tests",
            "performance_tests",
            "robustness_tests"
        }

        for _, target in ipairs(targets) do
            os.exec("xmake build " .. target)
            os.exec("xmake run " .. target)
        end
    end)

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro definition
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

-- Standalone xmake.lua for simple_server example
-- Note: To build from examples directory, run xmake from the project root
-- and use: xmake build example_simple_server

add_rules("mode.debug", "mode.release")
set_languages("c++23")

add_requires("libcurl")

target("simple_server")
    set_kind("binary")
    add_files("main.cpp")
    -- Include all source files from parent src directory
    add_files("../../src/*.cpp")
    add_includedirs("../../src")
    add_packages("libcurl")
    add_syslinks("pthread")
    add_defines("_CRT_SECURE_NO_WARNINGS")

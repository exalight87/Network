add_rules("mode.debug", "mode.release")
set_languages("c++23")

target("file_server")
    set_kind("binary")
    add_files("main.cpp")
    add_files("../../src/kernel/*.cpp")
    add_includedirs("../../include")
    if is_plat("windows") then
        add_syslinks("ws2_32")
        add_defines("_CRT_SECURE_NO_WARNINGS")
    else
        add_syslinks("pthread")
    end

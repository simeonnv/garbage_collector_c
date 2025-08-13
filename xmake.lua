add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "./"})

add_requires("c-vector")

-- Library target
target("gc")
    set_kind("static") -- or "shared"
    add_files("src/*.c")
    add_packages("c-vector")
    add_includedirs("include", {public = true})

-- Example program
target("basic_usage")
    set_kind("binary")
    add_files("examples/basic_usage.c")
    add_deps("gc") -- link with the gc library

-- Test program
target("test_gc")
    set_kind("binary")
    add_files("tests/test_gc.c")
    add_deps("gc")

set_project("PyC64")
set_version("0.1.0")
set_xmakever("2.8.5")
set_languages("c99")
set_warnings("allextra")
add_rules("mode.debug", "mode.release")
if is_plat("windows") then
    add_requires("libsdl2 2.32.10", {configs = {shared = false, runtimes = is_mode("debug") and "MTd" or "MT"}})
else
    add_requires("libsdl2", {system = true})
end

-- Prefer the native Windows toolchain unless the caller selects another one.
if is_plat("windows") and not get_config("toolchain") and not get_config("toolchains") then
    set_toolchains("msvc")
end

option("python")
    set_default("")
    set_showmenu(true)
    set_description("CPython 3.11+ for oracle and host-library tests (optional)")
option_end()

option("chargen_rom")
    set_default("")
    set_showmenu(true)
    set_description("C64 character ROM for full-machine tests")
option_end()

option("basic_rom")
    set_default("")
    set_showmenu(true)
    set_description("C64 BASIC ROM for float and full-machine tests")
option_end()

option("kernal_rom")
    set_default("")
    set_showmenu(true)
    set_description("C64 KERNAL ROM for math and full-machine tests")
option_end()

target("pyc64c")
    set_kind("binary")
    set_rundir("$(projectdir)")
    if is_mode("release") then
        set_optimize("faster")
    end
    add_includedirs("compiler", "platform")
    add_files("compiler/compiler.c", "compiler/lexer.c", "compiler/parser.c",
              "compiler/types.c", "compiler/infer.c", "compiler/modules.c", "compiler/ir.c", "compiler/optimize.c",
              "compiler/codegen.c", "compiler/classes.c", "compiler/enums.c", "compiler/bytes.c", "compiler/floats.c", "compiler/irq.c", "compiler/music.c", "compiler/assets.c", "platform/assets.c", "platform/rsid.c", "platform/desktop.c", "platform/project.c", "platform/cli.c")
    add_installfiles("stdlib/(**.py)", {prefixdir = "share/pyc64/stdlib"})
    add_installfiles("stdlib/(**.pyi)", {prefixdir = "share/pyc64/stdlib"})
    add_installfiles("platform/vendor/STB_LICENSE.txt", {prefixdir = "share/pyc64/licenses"})
    add_installfiles("LICENSE", {prefixdir = "share/pyc64/licenses"})
    add_cflags("-Wpedantic", {tools = {"gcc", "clang"}})
    on_config(function (target)
        -- xmake maps c99 to C++ for MSVC; use its supported C mode instead.
        if target:has_tool("cc", "cl", "clang_cl") then
            target:set("languages", "c11")
            target:set("runtimes", is_mode("debug") and "MTd" or "MT")
            target:add("defines", "_CRT_SECURE_NO_WARNINGS")
        end
    end)
target_end()

-- MSVC/clang-cl: static runtime and quiet CRT deprecation warnings.
rule("pyc64.msvc")
    on_config(function (target)
        if target:has_tool("cc", "cl", "clang_cl") or target:has_tool("cxx", "cl", "clang_cl") then
            target:set("runtimes", is_mode("debug") and "MTd" or "MT")
            target:add("defines", "_CRT_SECURE_NO_WARNINGS")
        end
    end)
rule_end()

-- Emulator cores are pinned git submodules in third_party/. Loading a target
-- that needs one runs `git submodule update --init` for it when it is missing.
target("chrono_core")
    set_kind("static")
    set_default(false)
    set_languages("c11")
    set_warnings("none")
    add_rules("pyc64.msvc")
    add_includedirs("third_party/chrono/src", {public = true})
    on_load(function (target)
        import("thirdparty", {rootdir = path.join(os.projectdir(), "tools")})
        local src = path.join(thirdparty.ensure("chrono"), "src")
        for _, file in ipairs(os.files(path.join(src, "*.c"))) do
            local name = path.filename(file)
            -- The PyC64 host replaces Chrono's own SDL front end.
            if name ~= "main.c" and name ~= "host.c" then target:add("files", file) end
        end
    end)
target_end()

target("chips_core")
    set_kind("static")
    set_default(false)
    set_languages("c11")
    set_warnings("none")
    add_rules("pyc64.msvc")
    add_includedirs("third_party/chips", {public = true})
    add_files("runner/chips.c")
    on_load(function (target)
        import("thirdparty", {rootdir = path.join(os.projectdir(), "tools")})
        thirdparty.ensure("chips")
    end)
target_end()

-- Interactive runner: ChronoCoders/C64 with an SDL window, audio and input.
target("pyc64run")
    set_kind("binary")
    set_languages("cxx17")
    add_rules("pyc64.msvc")
    add_deps("chrono_core")
    add_packages("libsdl2")
    add_files("runner/pyc64run.cpp", "runner/common.cpp", "runner/chrono_machine.cpp")
    add_defines("SDL_MAIN_HANDLED")
    if is_plat("linux") then add_syslinks("m") end
    add_installfiles("third_party/chrono/LICENSE", {prefixdir = "share/pyc64/licenses", filename = "CHRONO_LICENSE.txt"})
    add_installfiles("runner/SDL_LICENSE.txt", {prefixdir = "share/pyc64/licenses"})
target_end()

-- Headless tester: floooh/chips' 6502 and complete C64, no SDL dependency.
target("pyc64test")
    set_kind("binary")
    set_languages("cxx17")
    add_rules("pyc64.msvc")
    add_deps("chips_core")
    add_files("runner/pyc64test.cpp", "runner/common.cpp", "runner/chips_machine.cpp", "runner/chips_cpu.cpp")
    if is_plat("linux") then add_syslinks("m") end
    add_installfiles("third_party/chips/LICENSE", {prefixdir = "share/pyc64/licenses", filename = "CHIPS_LICENSE.txt"})
target_end()

-- Chrono's own unit suites, built from the submodule with two portability
-- patches applied to copies in the build directory (see tools/thirdparty.lua).
for _, suite in ipairs({"mem", "cpu", "cia", "sid", "vic", "drive", "via", "iec", "gcr", "snapshot"}) do
    target("chrono_test_" .. suite)
        set_kind("binary")
        set_default(false)
        set_languages("c11")
        set_warnings("none")
        add_rules("pyc64.msvc")
        add_deps("chrono_core")
        set_rundir("$(projectdir)")
        on_load(function (target)
            import("thirdparty", {rootdir = path.join(os.projectdir(), "tools")})
            target:add("files", thirdparty.chrono_test(target:name():sub(#"chrono_test_" + 1)))
        end)
        add_tests("unit", {runargs = (suite == "drive" or suite == "iec") and {"fast"} or {}, timeout = 180})
    target_end()
end

-- The compiler suite in tests/: each program, diagnostic, machine, CLI and
-- example check is one xmake test. See tests/README.md.
target("pyc64_tests")
    set_kind("phony")
    set_default(false)
    add_deps("pyc64c", "pyc64run", "pyc64test")
    on_load(function (target)
        import("suite", {rootdir = path.join(os.projectdir(), "tests")})
        suite.register(target)
    end)
    on_test(function (target, opt)
        import("suite", {rootdir = path.join(os.projectdir(), "tests")})
        return suite.run(target, opt)
    end)
target_end()

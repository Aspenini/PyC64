set_project("PyC64")
set_version("0.1.0")
set_xmakever("2.8.5")
set_languages("c99")
set_warnings("allextra")
add_rules("mode.debug", "mode.release")

-- Prefer the native Windows toolchain unless the caller selects another one.
if is_plat("windows") and not get_config("toolchain") and not get_config("toolchains") then
    set_toolchains("msvc")
end

option("python")
    set_default("")
    set_showmenu(true)
    set_description("Python 3.11+ interpreter for the integration tests")
option_end()

option("basic_rom")
    set_default("")
    set_showmenu(true)
    set_description("C64 BASIC ROM for optional floating-point CPU execution tests")
option_end()

option("kernal_rom")
    set_default("")
    set_showmenu(true)
    set_description("C64 KERNAL ROM for optional power and standard math CPU tests")
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
    add_tests("compiler_integration")
    on_test(function (target, opt)
        import("lib.detect.find_tool")
        local configured = get_config("python")
        local python
        if configured and configured ~= "" then
            python = {program = configured}
        else
            python = find_tool("python3") or find_tool("python")
        end
        if not python then
            return false, "Python 3.11+ is required to run tests; configure with xmake f --python=PATH"
        end
        local errors
        local ok = try {
            function ()
                local envs = {}
                local basic_rom = get_config("basic_rom")
                if basic_rom and basic_rom ~= "" then
                    envs.PYC64_BASIC_ROM = path.absolute(basic_rom)
                end
                local kernal_rom = get_config("kernal_rom")
                if kernal_rom and kernal_rom ~= "" then
                    envs.PYC64_KERNAL_ROM = path.absolute(kernal_rom)
                end
                os.vrunv(python.program,
                    {path.join(os.projectdir(), "tests", "test_compiler.py"),
                     path.absolute(target:targetfile())}, {curdir = os.projectdir(), envs = envs})
                return true
            end,
            catch {
                function (message)
                    errors = message
                end
            }
        }
        return ok == true, errors or ("Failed to run compiler integration tests with " .. python.program)
    end)
target_end()

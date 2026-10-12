-- Command-line tests for pyc64 (the project tool), pyc64c (the compiler),
-- pyc64test and pyc64run.
--
-- Each case receives the suite context `t` (see tests/suite.lua): t.exec runs
-- a program in the case's empty workspace t.work, t.check fails the test with
-- the command output attached.

local MAIN = 'print("Project works")\n'
local PROJECT = '[project]\nname = "demo"\nentry = "main.py"\n'

local function write(t, name, text)
    local file = path.join(t.work, name)
    os.mkdir(path.directory(file))
    io.writefile(file, text, {encoding = "binary"})
    return file
end

local function read(file)
    return os.isfile(file) and io.readfile(file, {encoding = "binary"}) or nil
end

-- The project tool; t.compiler is pyc64c itself.
local function pyc64(t, args, opt)
    return t.exec(t.tool, args, opt)
end

local function pyc64c(t, args, opt)
    return t.exec(t.compiler, args, opt)
end

local function succeeds(t, result, what)
    t.check(result.code == 0, what .. " failed", result)
    return result
end

local function fails(t, result, text, what)
    t.check(result.code ~= 0, (what or "command") .. " was expected to fail", result)
    if text then
        t.check(t.contains(result.stderr, text), (what or "command") .. " error lacks '" .. text .. "'", result)
    end
    return result
end

-- Runs a PRG on the tester and returns its printed output.
local function output_of(t, prg)
    return succeeds(t, t.exec(t.tester, {prg}), "pyc64test " .. path.filename(prg)).stdout
end

local function project(t, extra, main)
    write(t, "main.py", main or MAIN)
    return write(t, "pyc64.toml", PROJECT .. (extra or ""))
end

-- No PYC64_RUNNER leaks in from the developer's environment.
local CLEAN = {envs = {PYC64_RUNNER = ""}}

function cases()
    return {
        {name = "version_and_help"},
        {name = "compiler_options"},
        {name = "tool_finds_compiler"},
        {name = "test_command"},
        {name = "new_project_workflow"},
        {name = "new_rejects_bad_names"},
        {name = "option_validation"},
        {name = "missing_project"},
        {name = "project_paths_and_overrides"},
        {name = "project_toml_diagnostics"},
        {name = "clean_preserves_other_files"},
        {name = "output_protection"},
        {name = "check_asm_and_size"},
        {name = "asset_command"},
        {name = "sid_command"},
        {name = "run_selects_runner"},
        {name = "run_launches_pyc64run", requires = {"machine"}},
        {name = "tester_options"},
        {name = "runner_options"},
    }
end

function version_and_help(t)
    local result = succeeds(t, pyc64(t, {"--version"}), "--version")
    t.check(result.stdout:find("^PyC64 0%.1%.0") ~= nil, "unexpected version", result)
    result = succeeds(t, pyc64(t, {"--help"}), "--help")
    t.check(t.contains(result.stdout, "Usage: pyc64 COMMAND"), "help lacks usage", result)
    fails(t, pyc64(t, {"--unexpected"}), "unknown command")
    fails(t, pyc64(t, {"compile"}), "unknown command 'compile'")
    fails(t, pyc64(t, {"build", "--unexpected"}), "unknown option")
    t.check(pyc64(t, {}).code ~= 0, "pyc64 without a command should fail after printing help")
end

function new_project_workflow(t)
    succeeds(t, pyc64(t, {"new", "space-game"}), "new")
    local root = path.join(t.work, "space-game")
    local config = read(path.join(root, "pyc64.toml"))
    t.check(config and t.contains(config, 'name = "space-game"'), "pyc64.toml lacks the project name")
    t.check(os.isfile(path.join(root, "main.py")) and os.isfile(path.join(root, ".gitignore")),
            "new did not create main.py and .gitignore")
    for _, command in ipairs({"check", "size"}) do
        succeeds(t, pyc64(t, {command}, {curdir = root}), command)
        t.check(not os.exists(path.join(root, "build")), command .. " created a build directory")
    end
    succeeds(t, pyc64(t, {"build"}, {curdir = root}), "build")
    local prg = path.join(root, "build", "space-game.PRG")
    t.check(os.isfile(path.join(root, "build", "space-game.map")), "build wrote no symbol map")
    t.check(t.contains(output_of(t, prg), "HELLO FROM PYC64!"), "new project prints the wrong text")
    local before = read(path.join(root, "main.py"))
    fails(t, pyc64(t, {"new", "space-game"}), nil, "new over an existing project")
    t.check(read(path.join(root, "main.py")) == before, "new changed an existing project")
end

function new_rejects_bad_names(t)
    for _, name in ipairs({"../outside", ".", "..", "CON", "nul", "COM1", "LPT9", "bad.name", "two words",
                           string.rep("a", 64)}) do
        fails(t, pyc64(t, {"new", name}), nil, "new '" .. name .. "'")
    end
    t.check(#os.filedirs(path.join(t.work, "*")) == #os.files(path.join(t.work, "command*")),
            "rejected names created files")
end

function option_validation(t)
    for _, args in ipairs({{"new"}, {"new", "game", "-o", "out.PRG"}, {"clean", "main.py"},
                           {"clean", "-I", "."}, {"build", "--project"}, {"run", "--runner", ""}}) do
        fails(t, pyc64(t, args), nil, table.concat(args, " "))
    end
    fails(t, pyc64(t, {"check", "--runner", "pyc64run"}), "apply only to run")
    fails(t, pyc64(t, {"build", "--rom-dir", "roms"}), "apply only to run")
    fails(t, pyc64(t, {"build", "--", "--headless"}), "apply only to run")
    fails(t, pyc64(t, {"run", "--vice", "x64sc"}), "--vice was removed")
end

function missing_project(t)
    fails(t, pyc64(t, {"build"}), "cannot open project")
    t.check(not os.exists(path.join(t.work, "build")), "a failed project load created build/")
end

function project_paths_and_overrides(t)
    local config = write(t, "pyc64.toml", '[project]\nname = "space-game"\nentry = "sources/main.py"\n' ..
                         '[target]\nmachine = "c64-reu"\n[build]\ndirectory = "output/c64"\noptimize = "size"\n')
    write(t, "sources/main.py", 'from pyc64.math import clamp_u8\nimport helper\nprint(clamp_u8(10, 0, 5), helper.value)\n')
    write(t, "sources/helper.py", "value = 7\n")
    for _, command in ipairs({"build", "asm", "size", "check"}) do
        succeeds(t, pyc64(t, {command, "--project", config}, {curdir = t.root}), command .. " --project")
    end
    local built = path.join(t.work, "output", "c64", "space-game.PRG")
    t.check(t.contains(output_of(t, built), "5 7"), "project build prints the wrong values")
    t.check(t.contains(read(path.join(t.work, "output", "c64", "space-game.lst")) or "", "jsr"),
            "asm listing lacks instructions")
    local alternate = path.join(t.work, "custom.PRG")
    succeeds(t, pyc64(t, {"build", "--target", "c64", "-o", alternate, "--map", path.join(t.work, "custom.map")}),
             "build with overrides")
    t.check(read(alternate) == read(built), "a c64 override changed code that uses no REU")
    fails(t, pyc64(t, {"build", path.join(t.work, "sources", "main.py"), "--project", config}), "not both")
    -- An explicit SOURCE ignores an unrelated, even invalid, pyc64.toml.
    io.writefile(config, "invalid TOML", {encoding = "binary"})
    succeeds(t, pyc64(t, {"build", path.join(t.work, "sources", "main.py")}), "standalone build")
    t.check(os.isfile(path.join(t.work, "sources", "main.PRG")), "standalone build wrote no PRG")
end

function project_toml_diagnostics(t)
    local valid = PROJECT
    write(t, "main.py", MAIN)
    for _, case in ipairs({
            {"", "project.name is required"},
            {'[project]\nname = "demo"\nname = "again"', "duplicate project key"},
            {valid .. "[project]\n", "duplicate project table"},
            {valid .. 'unknown = "ignored"\n', "unsupported project key"},
            {valid .. '[unsupported]\ndirectory = "assets"', "unsupported project table"},
            {valid .. '[assets]\ndirectory = ""', "assets.directory must not be empty"},
            {valid .. '[runner]\nexecutable = ""', "runner.executable must not be empty"},
            {valid .. "[emulator]\nexecutable = 'x64sc'", "[emulator] was replaced by [runner]"},
            {"[project]\nname = 10", "single-line TOML string"},
            {'[project]\nname = "demo" trailing', "single-line TOML string"},
            {'[project]\nname = "../bad"', "project name"},
            {valid .. '[target]\nmachine = "amiga"', "unknown target"},
            {valid .. '[build]\noptimize = "fast"', "only build.optimize"},
            {valid .. '[build]\ndirectory = "../outside"', "child directory"},
            {valid .. '[build]\ndirectory = "/tmp"', "child directory"},
            {valid .. '[build]\ndirectory = "C:/outside"', "child directory"},
            {valid .. '[build]\ndirectory = "."', "child directory"},
            {valid .. '[build]\ndirectory = "build/.."', "child directory"},
            {valid .. "\0", "NUL byte"},
            {"#" .. string.rep("a", 17000), "16384 bytes"}}) do
        io.writefile(path.join(t.work, "pyc64.toml"), case[1], {encoding = "binary"})
        local result = fails(t, pyc64(t, {"build"}), case[2], "pyc64.toml " .. case[2])
        t.check(result.stderr:find("pyc64%.toml:%d+: error:") ~= nil, "diagnostic lacks pyc64.toml:LINE", result)
        t.check(not os.exists(path.join(t.work, "build")), "a rejected project created build/")
    end
    io.writefile(path.join(t.work, "pyc64.toml"), "# TOML comments\n[project] # name\nname='demo' # comment\n" ..
                 "entry = 'main.py'\n[build]\ndirectory = './build/nested'\n")
    succeeds(t, pyc64(t, {"build"}), "build with comments")
    t.check(os.isfile(path.join(t.work, "build", "nested", "demo.PRG")), "nested build directory unused")
end

function clean_preserves_other_files(t)
    local config = project(t)
    succeeds(t, pyc64(t, {"build"}), "build")
    succeeds(t, pyc64(t, {"asm"}), "asm")
    write(t, "build/other.PRG", "other program")
    write(t, "build/assets/data.bin", "asset")
    for _ = 1, 2 do
        succeeds(t, pyc64(t, {"clean", "--project", config}, {curdir = t.root}), "clean")
    end
    for _, extension in ipairs({"PRG", "map", "lst"}) do
        t.check(not os.exists(path.join(t.work, "build", "demo." .. extension)), "clean left demo." .. extension)
    end
    t.check(read(path.join(t.work, "build", "other.PRG")) == "other program", "clean removed another PRG")
    t.check(read(path.join(t.work, "build", "assets", "data.bin")) == "asset", "clean removed an asset")
    t.check(read(path.join(t.work, "main.py")) == MAIN, "clean changed the source")
end

function output_protection(t)
    local config = project(t)
    local before = read(config)
    for _, flag in ipairs({"-o", "--map"}) do
        fails(t, pyc64(t, {"build", flag, config}), "project configuration")
        fails(t, pyc64(t, {"build", flag, t.work .. "/./pyc64.toml"}), "project configuration")
    end
    t.check(read(config) == before, "a rejected build changed pyc64.toml")
    succeeds(t, pyc64(t, {"build"}), "build")
    local prg = path.join(t.work, "build", "demo.PRG")
    local previous = read(prg)
    write(t, "main.py", "print(missing)\n")
    fails(t, pyc64(t, {"build"}), "unknown variable")
    t.check(read(prg) == previous, "a failed build replaced the previous PRG")
    -- A standalone source can never be its own output.
    local source = write(t, "solo/main.py", "x = 5\n")
    fails(t, pyc64(t, {"build", source, "-o", t.work .. "/solo/./main.py"}))
    t.check(read(source) == "x = 5\n", "the source was overwritten")
    os.rm(prg)
    os.mkdir(prg)
    fails(t, pyc64(t, {"clean"}), nil, "clean of a directory")
    t.check(os.isdir(prg), "clean removed a directory")
end

function check_asm_and_size(t)
    local source = write(t, "main.py", "from pyc64.types import u8\nx: u8 = 5\nx += 1\nprint(x)\n")
    local result = succeeds(t, pyc64(t, {"check", source}), "check")
    t.check(not os.exists(path.join(t.work, "main.PRG")), "check wrote a PRG")
    result = succeeds(t, pyc64(t, {"size", source}), "size")
    t.check(result.stdout:find("Code: %d+ bytes") ~= nil, "size lacks a code size", result)
    succeeds(t, pyc64(t, {"asm", source}), "asm")
    local listing = read(path.join(t.work, "main.lst")) or ""
    t.check(t.contains(listing, "BASIC launcher") and t.contains(listing, "inc L"), "listing lacks code")
    for _, target in ipairs({"c64", "c64-reu", "c64-ultimate"}) do
        local prg = path.join(t.work, target .. ".PRG")
        succeeds(t, pyc64(t, {"build", source, "-o", prg, "--target", target}), "build " .. target)
        t.check(output_of(t, prg) == "6\n", target .. " build prints the wrong value")
    end
end

function asset_command(t)
    local assets = path.join(t.root, "examples", "assets", "assets")
    local result = succeeds(t, pyc64(t, {"asset", "sprite", path.join(assets, "ships.png"), "-o", "ships.spr"}),
                            "sprite conversion")
    t.check(t.contains(result.stdout, "128 bytes, 2 sprites"), "unexpected sprite summary", result)
    t.check(#read(path.join(t.work, "ships.spr")) == 128, "sprite output has the wrong size")
    result = succeeds(t, pyc64(t, {"asset", "charset", path.join(assets, "font.png"), "-o", "font.chr"}),
                      "charset conversion")
    t.check(#read(path.join(t.work, "font.chr")) == 2048, "charset output has the wrong size", result)
    write(t, "broken.png", "not a png")
    fails(t, pyc64(t, {"asset", "sprite", "broken.png", "-o", "broken.spr"}))
    t.check(not os.exists(path.join(t.work, "broken.spr")), "a failed conversion left output behind")
    fails(t, pyc64(t, {"asset", "unknown", path.join(assets, "ships.png")}))
end

function sid_command(t)
    local tunes = path.join(t.root, "examples", "music_standalone", "assets")
    local result = succeeds(t, pyc64(t, {"sid", path.join(tunes, "basic.sid"), "-o", "basic.PRG"}), "sid")
    t.check(t.contains(result.stdout, "Standalone RSID: song 1"), "sid summary missing", result)
    local data = read(path.join(t.work, "basic.PRG"))
    t.check(data and data:byte(1) == 1 and data:byte(2) == 8, "standalone PRG lacks the $0801 load address")
    write(t, "keep.PRG", "keep existing output")
    fails(t, pyc64(t, {"sid", path.join(tunes, "basic.sid"), "--song", "99", "-o", "keep.PRG"}), "does not exist")
    fails(t, pyc64(t, {"sid", path.join(t.root, "examples", "music", "assets", "theme.sid"), "-o", "keep.PRG"}),
          "requires an RSID")
    fails(t, pyc64(t, {"sid", path.join(tunes, "basic.sid"), "--runner", "x", "-o", "keep.PRG"}))
    t.check(read(path.join(t.work, "keep.PRG")) == "keep existing output", "a failed sid build replaced output")
    result = pyc64(t, {"sid", path.join(tunes, "basic.sid"), "-o", "played.PRG", "--run", "--runner",
                       "missing-sid-runner"}, CLEAN)
    t.check(result.code ~= 0 and t.contains(result.stdout, "Launching missing-sid-runner"),
            "sid --run did not launch the selected runner", result)
end

function run_selects_runner(t)
    project(t, "[runner]\nexecutable = 'missing-configured-runner'\n")
    local result = fails(t, pyc64(t, {"run"}, CLEAN), "cannot launch runner")
    t.check(t.contains(result.stdout, "Launching missing-configured-runner"), "project runner unused", result)
    t.check(os.isfile(path.join(t.work, "build", "demo.PRG")), "run did not build first")
    result = pyc64(t, {"run"}, {envs = {PYC64_RUNNER = "missing-environment-runner"}})
    t.check(t.contains(result.stdout, "Launching missing-environment-runner"), "PYC64_RUNNER unused", result)
    result = pyc64(t, {"run", "--runner", "missing-option-runner"},
                   {envs = {PYC64_RUNNER = "missing-environment-runner"}})
    t.check(t.contains(result.stdout, "Launching missing-option-runner"), "--runner does not take priority", result)
    -- A copy of pyc64c stands in for the runner and reports what it received,
    -- proving spaces, quotes and trailing backslashes survive as one argument.
    local stand_in = path.join(t.work, "runner with spaces" .. (is_host("windows") and ".exe" or ""))
    os.cp(t.compiler, stand_in)
    local odd = '--a b"c\\'
    result = fails(t, pyc64(t, {"run", "--runner", stand_in, "--", odd}, CLEAN), "unknown option '" .. odd .. "'")
    t.check(t.contains(result.stderr, "runner exited with status 1"), "runner status not reported", result)
    write(t, "pyc64.toml", PROJECT)
    succeeds(t, pyc64(t, {"build", "--target", "c64-reu"}), "c64-reu build")
    result = pyc64(t, {"run", "--target", "c64-reu", "--runner", stand_in}, CLEAN)
    t.check(t.contains(result.stderr, "does not emulate an REU") and t.contains(result.stdout, "Launching"),
            "c64-reu run should warn and still launch", result)
    write(t, "main.py", "print(missing)\n")
    result = fails(t, pyc64(t, {"run", "--runner", stand_in}, CLEAN), "unknown variable")
    t.check(not t.contains(result.stdout, "Launching"), "run launched after a failed build", result)
end

function run_launches_pyc64run(t)
    -- With no override, run uses the pyc64run installed beside pyc64c.
    project(t, nil, 'print("RUN VIA PYC64C")\n')
    local roms = t.config.roms
    local args = {"run", "--rom-dir", path.directory(roms.basic), "--", "--headless", "--frames", "30"}
    table.join2(args, t.rom_args(true))
    local result = succeeds(t, pyc64(t, table.join(args, {"--expect-screen", "RUN VIA PYC64C"}), CLEAN), "run")
    t.check(result.stdout:find("Launching [^\n]*pyc64run") ~= nil, "run did not use pyc64run", result)
    t.check(t.contains(result.stdout, "RUN VIA PYC64C"), "headless screen not shown", result)
    result = fails(t, pyc64(t, table.join(args, {"--expect-screen", "NOT ON SCREEN"}), CLEAN),
                   "expected screen text not found")
    t.check(t.contains(result.stderr, "runner exited with status 1"), "runner failure not reported", result)
end

function tester_options(t)
    local result = succeeds(t, t.exec(t.tester, {"--version"}), "pyc64test --version")
    t.check(t.contains(result.stdout, "pyc64test 0.1.0"), "wrong tester version", result)
    result = succeeds(t, t.exec(t.tester, {"--help"}), "pyc64test --help")
    t.check(t.contains(result.stdout, "--machine") and t.contains(result.stdout, "--trace"), "help incomplete", result)
    local bad = write(t, "bad.PRG", "\255\255\1\2")
    fails(t, t.exec(t.tester, {bad, "--entry", "65535"}), "PRG payload exceeds C64 memory")
    fails(t, t.exec(t.tester, {bad, "--frames", "5"}), "--frames is not available in CPU mode")
    fails(t, t.exec(t.tester, {bad, "--machine", "--trace", "t.txt"}), "--trace is not available in machine mode")
    fails(t, t.exec(t.tester, {bad, "--machine"}), "machine mode needs --expect or --expect-screen")
    fails(t, t.exec(t.tester, {bad, "--headless"}), "unknown option --headless")
    local source = write(t, "main.py", "from c64.memory import poke\npoke(0xC000, 42)\nprint(\"CPU RESULT\", 42)\n")
    local prg = path.join(t.work, "main.PRG")
    succeeds(t, pyc64(t, {"build", source, "-o", prg}), "build")
    result = succeeds(t, t.exec(t.tester, {prg, "--expect", "0xC000=42", "--expect", "$C000=$2A",
                                           "--expect-text", "CPU RESULT 42", "--expect-reg", "SP=0xF0",
                                           "--ram", "ram.bin", "--trace", "trace.txt"}), "checks")
    t.check(t.contains(result.stderr, "SP=$F0") and t.contains(result.stderr, "Checks passed"), "summary", result)
    t.check(#read(path.join(t.work, "ram.bin")) == 65536, "RAM dump has the wrong size")
    local trace = read(path.join(t.work, "trace.txt")) or ""
    t.check(t.contains(trace, " 080D ") and t.contains(trace, " 0200 "), "trace lacks entry and return")
    fails(t, t.exec(t.tester, {prg, "--expect", "0xC000=41"}), "RAM[49152] = 42, expected 41")
    fails(t, t.exec(t.tester, {prg, "--expect-reg", "SP=0"}), "SP = 240, expected 0")
    fails(t, t.exec(t.tester, {prg, "--expect-reg", "Q=1"}), "registers are A, X, Y, SP and P")
    fails(t, t.exec(t.tester, {prg, "--expect-text", "MISSING"}), "expected text not found")
    fails(t, t.exec(t.tester, {prg, "--cycles", "100"}), "cycle limit")
end

function runner_options(t)
    local result = succeeds(t, t.exec(t.runner, {"--version"}), "pyc64run --version")
    t.check(t.contains(result.stdout, "pyc64run 0.1.0"), "wrong runner version", result)
    result = succeeds(t, t.exec(t.runner, {"--help"}), "pyc64run --help")
    t.check(t.contains(result.stdout, "--headless"), "help lacks --headless", result)
    local bad = write(t, "bad.PRG", "\255\255\1\2")
    fails(t, t.exec(t.runner, {bad, "--headless", "--entry", "65535"}), "PRG payload exceeds C64 memory")
    fails(t, t.exec(t.runner, {bad, "--cycles", "5"}), "unknown option --cycles")
    fails(t, t.exec(t.runner, {bad, "--machine"}), "unknown option --machine")
    local source = write(t, "main.py", 'print("hi")\n')
    local prg = path.join(t.work, "main.PRG")
    succeeds(t, pyc64(t, {"build", source, "-o", prg}), "build")
    fails(t, t.exec(t.runner, {prg, "--headless", "--rom-dir", path.join(t.work, "no-roms")},
                    {envs = {PYC64_ROM_DIR = "", PYC64_BASIC_ROM = "", PYC64_KERNAL_ROM = "",
                             PYC64_CHARGEN_ROM = ""}}), "missing C64 ROMs")
end

function compiler_options(t)
    local result = succeeds(t, pyc64c(t, {"--version"}), "pyc64c --version")
    t.check(result.stdout:find("^PyC64 0%.1%.0") ~= nil, "unexpected compiler version", result)
    result = succeeds(t, pyc64c(t, {"--help"}), "pyc64c --help")
    t.check(t.contains(result.stdout, "Usage: pyc64c SOURCE") and t.contains(result.stdout, "--emit"),
            "compiler help incomplete", result)
    local source = write(t, "game.py", "from pyc64.types import u8\nx: u8 = 5\nx += 1\nprint(x)\n")
    succeeds(t, pyc64c(t, {source}), "pyc64c SOURCE")
    t.check(output_of(t, path.join(t.work, "game.PRG")) == "6\n", "default PRG prints the wrong value")
    succeeds(t, pyc64c(t, {source, "--emit", "asm"}), "--emit asm")
    t.check(t.contains(read(path.join(t.work, "game.lst")) or "", "inc L"), "default listing lacks code")
    os.rm(path.join(t.work, "game.PRG"))
    result = succeeds(t, pyc64c(t, {source, "--emit", "check"}), "--emit check")
    t.check(t.contains(result.stdout, "Checked") and not os.exists(path.join(t.work, "game.PRG")),
            "--emit check wrote output", result)
    result = succeeds(t, pyc64c(t, {source, "--emit", "size"}), "--emit size")
    t.check(result.stdout:find("Code: %d+ bytes") ~= nil and not os.exists(path.join(t.work, "game.PRG")),
            "--emit size wrote output or lacks sizes", result)
    write(t, "lib/helper.py", "value = 7\n")
    local main = write(t, "app/main.py", "import helper\nprint(helper.value)\n")
    fails(t, pyc64c(t, {main}), "cannot find module 'helper'")
    succeeds(t, pyc64c(t, {main, "-I", path.join(t.work, "lib"), "-o", "app.PRG", "--map", "app.map"}), "-I")
    t.check(os.isfile(path.join(t.work, "app.map")), "--map wrote nothing")
    t.check(output_of(t, path.join(t.work, "app.PRG")) == "7\n", "-I module prints the wrong value")
    fails(t, pyc64c(t, {source, "--emit", "elf"}), "unknown --emit 'elf'")
    fails(t, pyc64c(t, {source, "--target", "amiga"}), "unknown target")
    fails(t, pyc64c(t, {source, main}), "exactly one SOURCE")
    fails(t, pyc64c(t, {source, "--project", "pyc64.toml"}), "unknown option")
    fails(t, pyc64c(t, {}), nil, "pyc64c without SOURCE")
    for _, command in ipairs({"build", "run", "new", "clean", "asset", "sid", "test"}) do
        fails(t, pyc64c(t, {command, "main.py"}), "moved to the project tool: use 'pyc64 " .. command .. "'")
    end
end

function tool_finds_compiler(t)
    project(t)
    -- PYC64_COMPILER overrides the pyc64c installed beside pyc64.
    local result = fails(t, pyc64(t, {"build"}, {envs = {PYC64_COMPILER = "missing-compiler"}}),
                         "cannot launch compiler 'missing-compiler'")
    t.check(not os.exists(path.join(t.work, "build", "demo.PRG")), "a missing compiler produced a PRG", result)
    -- Compiler diagnostics and exit status pass straight through the tool.
    write(t, "main.py", "print(missing)\n")
    result = fails(t, pyc64(t, {"build"}), "unknown variable 'missing'")
    t.check(result.stderr:find("main%.py:1:7: error:") ~= nil, "diagnostic location lost", result)
    -- Project settings reach pyc64c as flags.
    write(t, "main.py", "from c64 import reu\nprint(reu.present())\n")
    write(t, "pyc64.toml", PROJECT .. '[target]\nmachine = "c64-reu"\n')
    succeeds(t, pyc64(t, {"build"}), "c64-reu project build")
    fails(t, pyc64(t, {"build", "--target", "c64"}), "requires target 'c64-reu'")
end

function test_command(t)
    project(t, nil, 'from c64.memory import poke\npoke(0xC000, 42)\nprint("TESTED", 42)\n')
    local result = succeeds(t, pyc64(t, {"test"}, CLEAN), "pyc64 test")
    t.check(result.stdout:find("Launching [^\n]*pyc64test") ~= nil, "test did not use pyc64test", result)
    t.check(t.contains(result.stdout, "TESTED 42") and t.contains(result.stderr, "Checks passed"),
            "tester output missing", result)
    succeeds(t, pyc64(t, {"test", "--", "--expect", "0xC000=42", "--expect-text", "TESTED 42"}), "forwarded checks")
    result = fails(t, pyc64(t, {"test", "--", "--expect", "0xC000=41"}), "RAM[49152] = 42, expected 41")
    t.check(t.contains(result.stderr, "tester exited with status 1"), "tester failure not reported", result)
    local source = write(t, "solo.py", 'print("SOLO")\n')
    result = succeeds(t, pyc64(t, {"test", source, "--", "--expect-text", "SOLO"}), "standalone test")
    t.check(os.isfile(path.join(t.work, "solo.PRG")), "standalone test wrote no PRG")
    result = fails(t, pyc64(t, {"test", "--tester", "missing-tester"}), "cannot launch tester 'missing-tester'")
    result = pyc64(t, {"test"}, {envs = {PYC64_TESTER = "missing-env-tester"}})
    t.check(result.code ~= 0 and t.contains(result.stdout, "Launching missing-env-tester"), "PYC64_TESTER unused",
            result)
    fails(t, pyc64(t, {"run", "--tester", "x"}), "apply only to run and test")
    fails(t, pyc64(t, {"test", "--runner", "x"}), "apply only to run and test")
    write(t, "main.py", "print(missing)\n")
    result = fails(t, pyc64(t, {"test"}), "unknown variable")
    t.check(not t.contains(result.stdout, "Launching"), "test ran after a failed build", result)
end

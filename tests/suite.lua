-- PyC64 test suite driver, loaded by the pyc64_tests target in xmake.lua.
--
-- Every test is a small source file whose comments state what must happen.
-- Nothing is generated ahead of time: each run compiles the source with
-- pyc64c and executes the PRG on pyc64test (chips) or pyc64run (Chrono).
-- tests/README.md documents the directives.

import("lib.detect.find_tool")

local root = os.projectdir()
local settings_warned = false
local tests = path.join(root, "tests")

-- Configuration -------------------------------------------------------------

local function settings()
    local configured = get_config("python")
    local name = is_host("windows") and "python" or "python3"
    local tool
    if configured and configured ~= "" then
        tool = find_tool(name, {program = configured, check = "--version"})
        if not tool and not settings_warned then
            settings_warned = true
            cprint("${yellow}pyc64_tests: --python=%s does not run; CPython checks are skipped", configured)
        end
    else
        tool = find_tool(name, {check = "--version"})
    end
    local python = tool and tool.program
    local roms = {}
    for _, kind in ipairs({"basic", "kernal", "chargen"}) do
        local rom = get_config(kind .. "_rom")
        if not rom or rom == "" then rom = os.getenv("PYC64_" .. kind:upper() .. "_ROM") end
        if rom and rom ~= "" then roms[kind] = path.absolute(rom, root) end
    end
    if roms.basic then
        local names = {kernal = {"kernal.rom", "kernal-901227-03.bin"},
                       chargen = {"chargen.rom", "chargen-901225-01.bin"}}
        for kind, candidates in pairs(names) do
            for _, name in ipairs(roms[kind] and {} or candidates) do
                local candidate = path.join(path.directory(roms.basic), name)
                if os.isfile(candidate) then roms[kind] = candidate; break end
            end
        end
    end
    return {python = python, roms = roms}
end

local function available(config, requirement)
    if requirement == "python" then return config.python ~= nil end
    if requirement == "roms" then return config.roms.basic and config.roms.kernal end
    if requirement == "machine" then return config.roms.basic and config.roms.kernal and config.roms.chargen end
    raise("unknown test requirement '%s'", requirement)
end

-- Directives ----------------------------------------------------------------

-- Splits an option string on spaces; double quotes group words. Backslashes
-- are literal, so "hello\r" reaches pyc64test as its own escape sequence.
local function split(text)
    local words, word, quoted = {}, nil, false
    for character in text:gmatch(".") do
        if character == '"' then
            quoted = not quoted
            word = word or ""
        elseif character:match("%s") and not quoted then
            if word then table.insert(words, word); word = nil end
        else
            word = (word or "") .. character
        end
    end
    if word then table.insert(words, word) end
    return words
end

local lists = {expect = true, screen = true, error = true}

local function directives_text(source)
    local found = {output = {}, expect = {}, screen = {}, error = {}, requires = {}}
    for line in (source .. "\n"):gmatch("(.-)\r?\n") do
        local text = line:match("^%s*#>(.*)$")
        if text then
            table.insert(found.output, (text:gsub("^ ", ""):gsub("%s+$", "")))
        else
            local key, value = line:match("^%s*#%s*([%w%-]+):%s*(.-)%s*$")
            if key == "requires" then
                table.join2(found.requires, split(value))
            elseif key and lists[key] then
                table.insert(found[key], value)
            elseif key then
                found[key] = value
            end
        end
    end
    return found
end

local function directives(file)
    return directives_text(io.readfile(file))
end

-- Discovery -----------------------------------------------------------------

local function source_of(entry)
    return os.isdir(entry) and path.join(entry, "main.py") or entry
end

local function entries(directory)
    local result = {}
    for _, file in ipairs(os.files(path.join(directory, "*.py"))) do
        table.insert(result, file)
    end
    for _, dir in ipairs(os.dirs(path.join(directory, "*"))) do
        if os.isfile(path.join(dir, "main.py")) then table.insert(result, dir) end
    end
    table.sort(result)
    return result
end

local function discover(config)
    local found = {}
    local function add(name, kind, entry, requires, extra)
        local case = {name = name, kind = kind, entry = entry, requires = requires or {}}
        for key, value in pairs(extra or {}) do case[key] = value end
        table.insert(found, case)
    end
    for _, area in ipairs(os.dirs(path.join(tests, "programs", "*"))) do
        for _, entry in ipairs(entries(area)) do
            local name = path.basename(area) .. "_" .. path.basename(entry)
            add(name, "program", entry, directives(source_of(entry)).requires)
        end
    end
    for _, file in ipairs(os.files(path.join(tests, "errors", "*.txt"))) do
        add("error_" .. path.basename(file), "error", file)
    end
    for _, entry in ipairs(entries(path.join(tests, "machine"))) do
        for _, backend in ipairs({"chips", "chrono"}) do
            add("machine_" .. backend .. "_" .. path.basename(entry), "machine", entry, {"machine"},
                {backend = backend})
        end
    end
    for _, file in ipairs(os.files(path.join(tests, "host", "*.py"))) do
        -- Files starting with "_" are shared helpers, not tests.
        if not path.filename(file):startswith("_") then
            add("host_" .. path.basename(file), "host", file, {"python"})
        end
    end
    import("cli", {rootdir = tests})
    for _, case in ipairs(cli.cases()) do
        add("cli_" .. case.name, "cli", nil, case.requires, {fn = case.name})
    end
    for _, dir in ipairs(os.dirs(path.join(root, "examples", "*"))) do
        if os.isfile(path.join(dir, "main.py")) or os.isfile(path.join(dir, "pyc64.toml")) then
            add("example_" .. path.basename(dir), "example", dir)
        end
    end
    return found
end

function register(target)
    local config = settings()
    local omitted = {}
    for _, case in ipairs(discover(config)) do
        local ok = true
        for _, requirement in ipairs(case.requires) do
            if not available(config, requirement) then
                ok = false
                omitted[requirement] = (omitted[requirement] or 0) + 1
            end
        end
        if ok then target:add("tests", case.name, {timeout = 300}) end
    end
    local hints = {roms = "--basic_rom and --kernal_rom", machine = "--basic_rom, --kernal_rom and --chargen_rom",
                   python = "--python (CPython 3.11+)"}
    for requirement, count in pairs(omitted) do
        cprint("${yellow}pyc64_tests: %d tests skipped; configure %s to enable them", count, hints[requirement])
    end
end

-- Execution helpers ---------------------------------------------------------

local function normalize(text)
    text = (text or ""):gsub("\r\n", "\n")
    local lines = {}
    for line in (text .. "\n"):gmatch("(.-)\n") do
        table.insert(lines, (line:gsub("%s+$", "")))
    end
    while #lines > 0 and lines[#lines] == "" do table.remove(lines) end
    return table.concat(lines, "\n")
end

local function new_context(target, case)
    local config = settings()
    local context = {
        root = root, config = config, case = case,
        compiler = path.absolute(target:dep("pyc64c"):targetfile()),
        tool = path.absolute(target:dep("pyc64"):targetfile()),
        tester = path.absolute(target:dep("pyc64test"):targetfile()),
        runner = path.absolute(target:dep("pyc64run"):targetfile()),
        work = path.join(root, "build", "tests", case.name),
        commands = 0,
    }
    os.tryrm(context.work)
    os.mkdir(context.work)

    -- Runs a program, capturing its exit code and normalized output.
    function context.exec(program, args, opt)
        opt = opt or {}
        context.commands = context.commands + 1
        local base = path.join(context.work, "command" .. context.commands)
        local stdin
        if opt.stdin then
            stdin = base .. ".in"
            io.writefile(stdin, opt.stdin, {encoding = "binary"})
        end
        local code = os.execv(program, args, {try = true, stdout = base .. ".out", stderr = base .. ".err",
                                              stdin = stdin, curdir = opt.curdir or context.work,
                                              envs = opt.envs})
        local function read(file) return os.isfile(file) and io.readfile(file) or "" end
        return {code = code, stdout = read(base .. ".out"):gsub("\r\n", "\n"),
                stderr = read(base .. ".err"):gsub("\r\n", "\n"),
                command = path.filename(program) .. " " .. table.concat(args, " ")}
    end

    -- Fails the test with the command's output appended.
    function context.check(condition, message, result)
        if condition then return end
        if result then
            message = string.format("%s\n  $ %s\n  exit %s\n  stdout: %s\n  stderr: %s", message,
                                    result.command, tostring(result.code),
                                    result.stdout:gsub("\n", "\n          "),
                                    result.stderr:gsub("\n", "\n          "))
        end
        raise(message)
    end

    function context.contains(text, part)
        return text:find(part, 1, true) ~= nil
    end

    function context.rom_args(machine)
        local roms = context.config.roms
        local args = {"--basic-rom", roms.basic, "--kernal-rom", roms.kernal}
        if machine then table.join2(args, {"--chargen-rom", roms.chargen}) end
        return args
    end

    function context.substitute(text)
        return (text:gsub("%${ROOT}", (root:gsub("\\", "/"))))
    end
    return context
end

-- Copies a test file or directory into the workspace as main.py so outputs
-- never land in tests/ and diagnostics name a stable file.
local function stage(context, entry)
    if os.isdir(entry) then
        os.cp(path.join(entry, "*"), context.work)
    else
        os.cp(entry, path.join(context.work, "main.py"))
    end
    return path.join(context.work, "main.py")
end

local function compile(context, source, extra)
    local prg = path.join(context.work, "main.PRG")
    local args = {source, "-o", prg, "--map", path.join(context.work, "main.map")}
    table.join2(args, extra or {})
    local result = context.exec(context.compiler, args)
    context.check(result.code == 0, "compilation failed", result)
    return prg
end

local function compare_output(context, actual, expected, what, result)
    actual, expected = normalize(actual), normalize(table.concat(expected, "\n"))
    if actual ~= expected then
        context.check(false, string.format("%s differs\n--- expected\n%s\n--- actual\n%s", what, expected, actual),
                      result)
    end
end

-- Test kinds ----------------------------------------------------------------

local runners = {}

function runners.program(context, case)
    local d = directives(source_of(case.entry))
    local source
    if d.source then
        -- "# source: examples/NAME/main.py" runs a program kept elsewhere.
        local file = path.join(root, d.source)
        os.cp(path.join(path.directory(file), "*"), context.work)
        source = path.join(context.work, path.filename(file))
    else
        source = stage(context, case.entry)
    end
    local prg = compile(context, source, split(d.pyc64c or ""))
    local args = {prg}
    if table.contains(d.requires, "roms") then table.join2(args, context.rom_args(false)) end
    table.join2(args, split(d.pyc64test or ""))
    for _, check in ipairs(d.expect) do table.join2(args, {"--expect", check}) end
    local result = context.exec(context.tester, args)
    if d.fails then
        context.check(result.code ~= 0, "program was expected to stop with: " .. d.fails, result)
        context.check(context.contains(result.stderr, d.fails), "missing failure '" .. d.fails .. "'", result)
    else
        context.check(result.code == 0, "pyc64test reported a failure", result)
    end
    compare_output(context, result.stdout, d.output, "program output", result)
    if d.oracle == "cpython" and context.config.python and not d.fails then
        local host = context.exec(context.config.python, {source},
                                  {envs = {PYTHONPATH = path.join(root, "stdlib"), PYTHONIOENCODING = "ascii"},
                                   stdin = d.stdin and d.stdin:gsub("\\n", "\n"):gsub("\\r", "\n")})
        context.check(host.code == 0, "CPython could not run the program", host)
        compare_output(context, host.stdout:upper(), d.output, "CPython output", host)
    end
end

-- Error case files hold several compile-failure cases separated by "# ---".
-- "# file: NAME.py" starts another source file within a case (main.py first).
local function error_cases(file)
    local cases, current = {}, nil
    local function start()
        current = {files = {}, order = {}, text = "", name = "main.py", first = nil}
        table.insert(cases, current)
    end
    start()
    for line in (io.readfile(file) .. "\n"):gmatch("(.-)\r?\n") do
        if line:match("^#%s*%-%-%-") then
            start()
        else
            local name = line:match("^#%s*file:%s*(%S+)%s*$")
            local directive = line:match("^#%s*([%w%-]+):")
            if name then
                current.name = name
            elseif directive == "error" or directive == "at" or directive == "pyc64c" then
                -- Directives stay out of the sources so line numbers match the code.
                current.text = current.text .. line .. "\n"
            else
                if not current.files[current.name] then
                    current.files[current.name] = ""
                    table.insert(current.order, current.name)
                end
                current.files[current.name] = current.files[current.name] .. line .. "\n"
                current.text = current.text .. line .. "\n"
                if not current.first and line:match("%S") and not line:match("^%s*#") then
                    current.first = line
                end
            end
        end
    end
    local result = {}
    for _, case in ipairs(cases) do
        if case.first then table.insert(result, case) end
    end
    return result
end

local function check_error_case(context, index, item)
    do
        local d = directives_text(item.text)
        local label = string.format("case %d (%s)", index, item.first)
        local work = path.join(context.work, "case" .. index)
        os.mkdir(work)
        for name, text in pairs(item.files) do
            io.writefile(path.join(work, name), text, {encoding = "binary"})
        end
        local prg = path.join(work, "main.PRG")
        local args = {path.join(work, "main.py"), "-o", prg}
        table.join2(args, split(d.pyc64c or ""))
        local result = context.exec(context.compiler, args, {curdir = work})
        context.check(result.code ~= 0, label .. ": compilation was expected to fail", result)
        context.check(not os.isfile(prg), label .. ": failed compilation wrote a PRG", result)
        context.check(#d.error > 0, label .. ": no '# error:' directive")
        for _, text in ipairs(d.error) do
            context.check(context.contains(result.stderr, text), label .. ": diagnostic lacks '" .. text .. "'",
                          result)
        end
        local file, line = result.stderr:match("([%w_%.%-]+):(%d+):%d+: error:")
        if not d["no-location"] then
            context.check(file ~= nil, label .. ": diagnostic lacks FILE:LINE:COLUMN: error:", result)
        end
        if d.at then
            local want_file, want_line = d.at:match("^(.-):(%d+)$")
            if not want_file then want_file, want_line = "main.py", d.at end
            context.check(file == want_file and line == want_line,
                          string.format("%s: diagnostic at %s:%s, expected %s:%s", label, tostring(file),
                                        tostring(line), want_file, want_line), result)
        end
    end
end

-- Every case runs; the test reports all failing cases together.
function runners.error(context, case)
    local cases = error_cases(case.entry)
    context.check(#cases > 0, "error file has no cases")
    local failures = {}
    for index, item in ipairs(cases) do
        try {function () check_error_case(context, index, item) end,
             catch {function (errors) table.insert(failures, tostring(errors)) end}}
    end
    context.check(#failures == 0, table.concat(failures, "\n"))
end

function runners.machine(context, case)
    local d = directives(source_of(case.entry))
    local prg
    if d.build then
        prg = path.join(context.work, "main.PRG")
        local args = {"build"}
        for _, word in ipairs(split(context.substitute(d.build))) do table.insert(args, word) end
        table.join2(args, {"-o", prg, "--map", path.join(context.work, "main.map")})
        local result = context.exec(context.tool, args)
        context.check(result.code == 0, "compilation failed", result)
    else
        prg = compile(context, stage(context, case.entry), split(d.pyc64c or ""))
    end
    local args = {prg, case.backend == "chips" and "--machine" or "--headless", "--frames", d.frames or "120"}
    table.join2(args, context.rom_args(true))
    for _, text in ipairs(d.screen) do table.join2(args, {"--expect-screen", text}) end
    for _, check in ipairs(d.expect) do table.join2(args, {"--expect", check}) end
    if d.input then table.join2(args, {"--input", d.input}) end
    if d.key then table.join2(args, {"--key", d.key}) end
    if d.joystick then table.join2(args, {"--joystick", d.joystick}) end
    local result = context.exec(case.backend == "chips" and context.tester or context.runner, args)
    context.check(result.code == 0, case.backend .. " machine checks failed", result)
end

function runners.host(context, case)
    local separator = is_host("windows") and ";" or ":"
    local result = context.exec(context.config.python, {case.entry},
                                {envs = {PYTHONPATH = path.join(root, "stdlib") .. separator ..
                                                      path.join(tests, "host")}})
    context.check(result.code == 0, "host library test failed", result)
end

function runners.cli(context, case)
    import("cli", {rootdir = tests})
    cli[case.fn](context)
end

function runners.example(context, case)
    -- Projects build through pyc64; plain examples go straight to pyc64c.
    local project = path.join(case.entry, "pyc64.toml")
    local prg = path.join(context.work, "main.PRG")
    local program, args = context.compiler, {path.join(case.entry, "main.py")}
    if os.isfile(project) then
        program, args = context.tool, {"build", "--project", project}
    end
    table.join2(args, {"-o", prg, "--map", path.join(context.work, "main.map")})
    local result = context.exec(program, args)
    context.check(result.code == 0, "example does not compile", result)
    local data = io.readfile(prg, {encoding = "binary"})
    context.check(data and #data > 14 and data:byte(1) == 1 and data:byte(2) == 8,
                  "example PRG lacks the $0801 BASIC load address", result)
end

function run(target, opt)
    local name = opt.name:match("[^/]+$") -- xmake passes TARGET/TEST
    local case
    for _, candidate in ipairs(discover(settings())) do
        if candidate.name == name then case = candidate; break end
    end
    if not case then
        cprint("${red}FAIL${clear} %s: unknown test", name)
        return false
    end
    local context = new_context(target, case)
    local ok = try {
        function ()
            runners[case.kind](context, case)
            return true
        end,
        catch {
            function (errors)
                cprint("${red}FAIL${clear} %s: %s", name, tostring(errors))
                cprint("${dim}  workspace: %s", context.work)
            end
        }
    }
    if ok then os.tryrm(context.work) end
    return ok == true
end

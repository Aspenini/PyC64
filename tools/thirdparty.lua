-- Pinned emulator submodules (see .gitmodules) and build-time test patches.

-- Returns third_party/NAME, initializing the submodule first when it is empty.
function ensure(name)
    local root = os.projectdir()
    local directory = path.join(root, "third_party", name)
    if #os.filedirs(path.join(directory, "*")) > 0 then
        return directory
    end
    cprint("${dim}initializing submodule third_party/%s", name)
    local ok = try {function ()
        os.vrunv("git", {"-C", root, "submodule", "update", "--init", "--", "third_party/" .. name})
        return true
    end}
    if not ok or #os.filedirs(path.join(directory, "*")) == 0 then
        raise("third_party/%s is missing; run `git submodule update --init` in a git checkout", name)
    end
    return directory
end

-- Copies a Chrono unit suite and its test.h into build/chrono-tests with two
-- portability fixes, leaving the submodule untouched:
--   * test.h asks for a zero-size line buffer, which MSVC's CRT rejects;
--   * snapshot_test.c writes scratch files under the POSIX-only /tmp/.
-- Returns the patched suite source.
function chrono_test(suite)
    local source = path.join(ensure("chrono"), "test")
    local output = path.join(os.projectdir(), "build", "chrono-tests")
    local function copy(name, replacements)
        local text = io.readfile(path.join(source, name))
        for _, pair in ipairs(replacements) do
            text = text:replace(pair[1], pair[2], {plain = true})
        end
        local target = path.join(output, name)
        if not os.isfile(target) or io.readfile(target) ~= text then
            io.writefile(target, text)
        end
        return target
    end
    copy("test.h", {{"setvbuf(stdout, NULL, _IOLBF, 0)", "setvbuf(stdout, NULL, _IONBF, 0)"}})
    os.mkdir(path.join(os.projectdir(), "build", "chrono-tests", "scratch"))
    return copy(suite .. "_test.c", {{"\"/tmp/", "\"build/chrono-tests/scratch/"}})
end

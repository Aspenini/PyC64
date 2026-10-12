#include "tool.h"

/* pyc64: the project tool. Building means invoking pyc64c; run and test
   hand the PRG to pyc64run and pyc64test. */

static void usage(void) {
  puts("PyC64 " VERSION " - C64 project tool\n"
       "Usage: pyc64 COMMAND [options]\n\n"
       "Commands:\n"
       "  new NAME            Create NAME/ with main.py, pyc64.toml, .gitignore\n"
       "  build [SOURCE]      Compile to a PRG (and a symbol map for projects)\n"
       "  run [SOURCE]        Build, then open the PRG in pyc64run\n"
       "  test [SOURCE]       Build, then check the PRG with pyc64test\n"
       "  check [SOURCE]      Check the program without writing output\n"
       "  size [SOURCE]       Report code, data and asset sizes\n"
       "  asm [SOURCE]        Write a readable instruction listing\n"
       "  clean               Remove the project's PRG, listing and map\n"
       "  asset KIND PNG      Prepare a sprite, charset, tile map or bitmap\n"
       "  sid FILE.sid        Prepare a standalone RSID player PRG\n\n"
       "Without SOURCE, commands use pyc64.toml in the current directory\n"
       "(or --project FILE) and write to its build directory.\n\n"
       "Build options: -o OUTPUT, --map FILE, --target c64|c64-reu|"
       "c64-ultimate,\n"
       "               -I DIR (repeatable), --project FILE\n"
       "run options:   --runner FILE, --rom-dir DIR, -- RUNNER-OPTIONS\n"
       "test options:  --tester FILE, --rom-dir DIR, -- TESTER-OPTIONS\n\n"
       "pyc64c, pyc64run and pyc64test are found beside pyc64; PYC64_COMPILER,\n"
       "PYC64_RUNNER and PYC64_TESTER (or a project's [runner] executable)\n"
       "select others. See 'pyc64 asset --help' and 'pyc64run --help'.");
}

static int source_output(char *out, const char *source, const char *extension) {
  char *dot, *slash, *back;
  if (strlen(source) + strlen(extension) + 1 >= MAX_PATH) {
    fputs("error: source path is too long\n", stderr);
    return 0;
  }
  strcpy(out, source);
  slash = strrchr(out, '/');
  back = strrchr(out, '\\');
  dot = strrchr(out, '.');
  if (dot && (!slash || dot > slash) && (!back || dot > back))
    *dot = 0;
  strcat(out, extension);
  return 1;
}

static int known(const char *command) {
  const char *commands[] = {"new", "build", "run",  "test",
                            "check", "size", "asm", "clean"};
  size_t i;
  for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
    if (!strcmp(command, commands[i]))
      return 1;
  return 0;
}

int main(int argc, char **argv) {
  Project project;
  const char *command, *source = NULL, *output = NULL, *map = NULL;
  const char *configuration = NULL, *runner = NULL, *tester = NULL;
  const char *rom_dir = NULL, *machine = NULL, *assets = NULL;
  const char *includes[MAX_SEARCH_PATHS];
  const char *args[MAX_SEARCH_PATHS * 2 + 16];
  char default_output[MAX_PATH], default_map[MAX_PATH], prg[MAX_PATH];
  char compiler[MAX_PATH], program[MAX_PATH];
  char **forwarded = NULL;
  int i, count = 0, nincludes = 0, options = 0, nforwarded = -1, is_project;
  int compiled;
  if (argc == 1 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") ||
      !strcmp(argv[1], "help")) {
    usage();
    return argc == 1 ? 1 : 0;
  }
  if (!strcmp(argv[1], "--version")) {
    puts("PyC64 " VERSION);
    return 0;
  }
  command = argv[1];
  if (!strcmp(command, "asset"))
    return tool_asset_command(argc - 2, argv + 2);
  if (!strcmp(command, "sid"))
    return tool_sid_command(argv[0], argc - 2, argv + 2);
  if (!known(command)) {
    fprintf(stderr, "error: unknown command '%s'; see pyc64 --help\n",
            command);
    return 1;
  }
  for (i = 2; i < argc; ++i) {
    const char *s = argv[i];
    if (!strcmp(s, "--")) {
      forwarded = argv + i + 1;
      nforwarded = argc - i - 1;
      break;
    }
    if (!strcmp(s, "--vice")) {
      fputs("error: --vice was removed; run uses pyc64run (select another "
            "runner with --runner FILE)\n",
            stderr);
      return 1;
    }
    if (!strcmp(s, "-o") || !strcmp(s, "--map") || !strcmp(s, "--target") ||
        !strcmp(s, "-I") || !strcmp(s, "--project") || !strcmp(s, "--runner") ||
        !strcmp(s, "--tester") || !strcmp(s, "--rom-dir")) {
      if (++i == argc || !*argv[i]) {
        fprintf(stderr, "error: %s requires a value\n", s);
        return 1;
      }
      if (strcmp(s, "--project"))
        ++options;
      if (!strcmp(s, "-I")) {
        if (nincludes == MAX_SEARCH_PATHS) {
          fputs("error: too many search paths\n", stderr);
          return 1;
        }
        includes[nincludes++] = argv[i];
      } else if (!strcmp(s, "-o"))
        output = argv[i];
      else if (!strcmp(s, "--map"))
        map = argv[i];
      else if (!strcmp(s, "--target"))
        machine = argv[i];
      else if (!strcmp(s, "--runner"))
        runner = argv[i];
      else if (!strcmp(s, "--tester"))
        tester = argv[i];
      else if (!strcmp(s, "--rom-dir"))
        rom_dir = argv[i];
      else
        configuration = argv[i];
    } else if (s[0] == '-') {
      fprintf(stderr, "error: unknown option '%s'\n", s);
      return 1;
    } else if (source) {
      fputs("error: only one source file or project name is supported\n",
            stderr);
      return 1;
    } else
      source = s;
  }
  if (!strcmp(command, "new")) {
    if (!source || configuration || options || nforwarded >= 0) {
      fputs("error: use 'pyc64 new NAME' without build options\n", stderr);
      return 1;
    }
    return project_new(source);
  }
  if ((runner && strcmp(command, "run")) ||
      (tester && strcmp(command, "test")) ||
      ((rom_dir || nforwarded >= 0) && strcmp(command, "run") &&
       strcmp(command, "test"))) {
    fputs("error: --runner, --tester, --rom-dir and -- apply only to run "
          "and test\n",
          stderr);
    return 1;
  }
  if (!strcmp(command, "clean") && (source || options)) {
    fputs("error: clean takes only an optional --project FILE\n", stderr);
    return 1;
  }
  if (source && configuration) {
    fputs("error: specify SOURCE or --project FILE, not both\n", stderr);
    return 1;
  }
  is_project = !source;
  if (is_project) {
    if (!project_load(&project, configuration ? configuration : "pyc64.toml"))
      return 1;
    if (!strcmp(command, "clean"))
      return project_clean(&project);
    source = project.entry;
    if (*project.assets)
      assets = project.assets;
    if (!machine)
      machine = project.machine;
    if (!output) {
      if (!project_artifact(&project, !strcmp(command, "asm") ? ".lst" : ".PRG",
                            default_output))
        return 1;
      output = default_output;
    }
    if (!map && strcmp(command, "check") && strcmp(command, "size")) {
      if (!project_artifact(&project, ".map", default_map))
        return 1;
      map = default_map;
    }
    if (platform_same_file(project.file, output) ||
        (map && platform_same_file(project.file, map))) {
      fputs("error: outputs must not overwrite project configuration\n",
            stderr);
      return 1;
    }
    if (strcmp(command, "check") && strcmp(command, "size") &&
        (output == default_output || map == default_map) &&
        !project_directory(&project, 1))
      return 1;
  } else if (!output && strcmp(command, "check") && strcmp(command, "size")) {
    if (!source_output(default_output, source,
                       !strcmp(command, "asm") ? ".lst" : ".PRG"))
      return 1;
    output = default_output;
  }

  /* Everything a project implies reaches pyc64c as ordinary flags. */
  tool_locate(argv[0], "pyc64c", "PYC64_COMPILER", compiler);
  args[count++] = compiler;
  args[count++] = source;
  args[count++] = "--emit";
  args[count++] = !strcmp(command, "check") ? "check"
                  : !strcmp(command, "size") ? "size"
                  : !strcmp(command, "asm")  ? "asm"
                                             : "prg";
  if (output) {
    args[count++] = "-o";
    args[count++] = output;
  }
  if (map) {
    args[count++] = "--map";
    args[count++] = map;
  }
  if (machine) {
    args[count++] = "--target";
    args[count++] = machine;
  }
  if (assets) {
    args[count++] = "--asset-dir";
    args[count++] = assets;
  }
  for (i = 0; i < nincludes; ++i) {
    args[count++] = "-I";
    args[count++] = includes[i];
  }
  args[count] = NULL;
  compiled = tool_spawn(args, "compiler",
                        "reinstall PyC64 or set PYC64_COMPILER");
  if (compiled)
    return 1;
  if (strcmp(command, "run") && strcmp(command, "test"))
    return 0;

  if (!desktop_absolute(output, prg))
    return 1;
  if (machine && !strcmp(machine, "c64-reu"))
    fprintf(stderr, "warning: %s does not emulate an REU; c64-reu programs see "
                    "no expansion\n",
            !strcmp(command, "run") ? "pyc64run" : "pyc64test");
  if (!strcmp(command, "run")) {
    /* --runner, then PYC64_RUNNER, then [runner] executable, then beside. */
    const char *environment = getenv("PYC64_RUNNER");
    const char *chosen = runner                        ? runner
                         : environment && *environment ? environment
                         : is_project && *project.runner ? project.runner
                                                         : NULL;
    if (chosen && strlen(chosen) >= MAX_PATH) {
      fputs("error: runner path is too long\n", stderr);
      return 1;
    }
    if (chosen)
      strcpy(program, chosen);
    else
      tool_locate(argv[0], "pyc64run", NULL, program);
    return tool_launch(program, "runner", "use --runner PATH or PYC64_RUNNER",
                       prg, rom_dir, forwarded,
                       nforwarded < 0 ? 0 : nforwarded);
  }
  if (tester && strlen(tester) >= MAX_PATH) {
    fputs("error: tester path is too long\n", stderr);
    return 1;
  }
  if (tester)
    strcpy(program, tester);
  else
    tool_locate(argv[0], "pyc64test", "PYC64_TESTER", program);
  return tool_launch(program, "tester", "use --tester PATH or PYC64_TESTER",
                     prg, rom_dir, forwarded, nforwarded < 0 ? 0 : nforwarded);
}

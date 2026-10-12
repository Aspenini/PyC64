#include "desktop.h"

static void usage(void) {
  puts("PyC64 " VERSION " - native C64 compiler\n"
       "Usage: pyc64c [build|check|asm|size|run] [SOURCE] [-o OUTPUT]\n"
       "              [--target c64|c64-reu|c64-ultimate] [--map FILE] [-I "
       "DIR]\n"
       "              [--project FILE] [--runner FILE] [--rom-dir DIR]\n"
       "              [-- RUNNER-OPTIONS...]\n"
       "       pyc64c new NAME\n"
       "       pyc64c clean [--project FILE]\n"
       "       pyc64c asset "
       "sprite|sprite-multicolor|charset|tileset|tilemap|bitmap|bitmap-screen "
       "SOURCE [-o "
       "OUTPUT]\n"
       "              [--palette RRGGBB,... (4 sprite or 16 bitmap colors)]\n"
       "       pyc64c --version\n"
       "       pyc64c sid FILE.sid [-o FILE.PRG] [--song 0..256] [--map FILE]\n"
       "                   [--run] [--runner FILE] [--rom-dir DIR]\n"
       "Without SOURCE, read pyc64.toml in the current directory.\n"
       "Project output: build/NAME.PRG (plus a symbol map).\n"
       "With SOURCE, default output replaces its extension with .PRG or .lst.\n"
       "run builds, then opens the PRG in pyc64run and waits for it to close.\n"
       "--runner, --rom-dir and options after -- apply only to run; see\n"
       "'pyc64run --help' for runner options such as --headless.\n"
       "clean removes only the named project's default PRG, listing and map.\n"
       "All profiles generate native C64 instructions; expansion APIs require "
       "their target.");
}

static int target(const char *name) {
  if (!strcmp(name, "c64"))
    return 0;
  if (!strcmp(name, "c64-reu"))
    return 1;
  if (!strcmp(name, "c64-ultimate"))
    return 2;
  fprintf(stderr, "error: unknown target '%s'\n", name);
  return -1;
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

int main(int argc, char **argv) {
  Compiler *c;
  Project project;
  const char *command = "build", *source = NULL, *output = NULL, *map = NULL;
  const char *configuration = NULL, *runner = NULL, *rom_dir = NULL;
  const char *machine = NULL;
  char default_output[MAX_PATH], default_map[MAX_PATH], run_output[MAX_PATH];
  char runner_path[MAX_PATH];
  char **forwarded = NULL;
  int i, rc = 1, is_project, options = 0, nforwarded = -1;
  if (argc > 1 && !strcmp(argv[1], "asset"))
    return desktop_asset_command(argc - 2, argv + 2);
  if (argc > 1 && !strcmp(argv[1], "sid"))
    return desktop_sid_command(argv[0], argc - 2, argv + 2);
  c = (Compiler *)calloc(1, sizeof(Compiler));
  if (!c) {
    fputs("error: unable to acquire compiler arena\n", stderr);
    return 1;
  }
  for (i = 1; i < argc; ++i) {
    const char *s = argv[i];
    if (!strcmp(s, "--version")) {
      puts("PyC64 " VERSION);
      rc = 0;
      goto done;
    }
    if (!strcmp(s, "--")) {
      forwarded = argv + i + 1;
      nforwarded = argc - i - 1;
      break;
    }
    if (!strcmp(s, "--vice")) {
      fputs("error: --vice was removed; run uses pyc64run (select another "
            "runner with --runner FILE)\n",
            stderr);
      goto done;
    }
    if (!strcmp(s, "--help") || !strcmp(s, "-h")) {
      usage();
      rc = 0;
      goto done;
    }
    if (!strcmp(s, "-o") || !strcmp(s, "--map") || !strcmp(s, "--target") ||
        !strcmp(s, "-I") || !strcmp(s, "--project") || !strcmp(s, "--runner") ||
        !strcmp(s, "--rom-dir")) {
      if (++i == argc || !*argv[i]) {
        fprintf(stderr, "error: %s requires a value\n", s);
        goto done;
      }
      if (strcmp(s, "--project"))
        ++options;
      if (!strcmp(s, "-I")) {
        if (c->nsearch_paths == MAX_SEARCH_PATHS ||
            strlen(argv[i]) >= MAX_PATH) {
          fputs("error: too many search paths or search path is too long\n",
                stderr);
          goto done;
        }
        strcpy(c->search_paths[c->nsearch_paths++], argv[i]);
      } else if (!strcmp(s, "-o"))
        output = argv[i];
      else if (!strcmp(s, "--map"))
        map = argv[i];
      else if (!strcmp(s, "--target"))
        machine = argv[i];
      else if (!strcmp(s, "--runner"))
        runner = argv[i];
      else if (!strcmp(s, "--rom-dir"))
        rom_dir = argv[i];
      else
        configuration = argv[i];
    } else if (i == 1 &&
               (!strcmp(s, "build") || !strcmp(s, "check") ||
                !strcmp(s, "asm") || !strcmp(s, "size") || !strcmp(s, "run") ||
                !strcmp(s, "new") || !strcmp(s, "clean")))
      command = s;
    else if (s[0] == '-') {
      fprintf(stderr, "error: unknown option '%s'\n", s);
      goto done;
    } else if (source) {
      fputs("error: only one source file or project name is supported\n",
            stderr);
      goto done;
    } else
      source = s;
  }
  if (!strcmp(command, "new")) {
    if (!source || configuration || options) {
      fputs("error: use 'pyc64c new NAME' without build options\n", stderr);
      goto done;
    }
    rc = project_new(source);
    goto done;
  }
  if ((runner || rom_dir || nforwarded >= 0) && strcmp(command, "run")) {
    fputs("error: --runner, --rom-dir and -- apply only to run\n", stderr);
    goto done;
  }
  if (!strcmp(command, "clean") && (source || options)) {
    fputs("error: clean takes only an optional --project FILE\n", stderr);
    goto done;
  }
  if (source && configuration) {
    fputs("error: specify SOURCE or --project FILE, not both\n", stderr);
    goto done;
  }
  is_project = !source;
  if (is_project) {
    if (!project_load(&project, configuration ? configuration : "pyc64.toml"))
      goto done;
    if (!strcmp(command, "clean")) {
      rc = project_clean(&project);
      goto done;
    }
    source = project.entry;
    strcpy(c->asset_root, project.assets);
    if (!machine)
      machine = project.machine;
    if (!output) {
      if (!project_artifact(&project, !strcmp(command, "asm") ? ".lst" : ".PRG",
                            default_output))
        goto done;
      output = default_output;
    }
    if (!map && (!strcmp(command, "build") || !strcmp(command, "run") ||
                 !strcmp(command, "asm"))) {
      if (!project_artifact(&project, ".map", default_map))
        goto done;
      map = default_map;
    }
    if (platform_same_file(project.file, output) ||
        (map && platform_same_file(project.file, map))) {
      fputs("error: outputs must not overwrite project configuration\n",
            stderr);
      goto done;
    }
    if (strcmp(command, "check") &&
        (output == default_output || map == default_map) &&
        (strcmp(command, "size") || map) &&
        !desktop_project_directory(&project, 1))
      goto done;
  } else if (!output) {
    if (!source_output(default_output, source,
                       !strcmp(command, "asm") ? ".lst" : ".PRG"))
      goto done;
    output = default_output;
  }
  if (machine) {
    c->target = target(machine);
    if (c->target < 0)
      goto done;
  }
  desktop_stdlib(c, argv[0]);
  rc = compile_source(c, !strcmp(command, "run") ? "build" : command, source,
                      output, map);
  if (!rc && !strcmp(command, "run")) {
    const char *env = getenv("PYC64_RUNNER");
    if (!desktop_absolute(output, run_output)) {
      rc = 1;
      goto done;
    }
    if (!runner) {
      if (env && *env)
        runner = env;
      else if (is_project && *project.runner)
        runner = project.runner;
      else {
        desktop_default_runner(argv[0], runner_path);
        runner = runner_path;
      }
    }
    if (c->target == 1)
      fputs("warning: pyc64run does not emulate an REU; c64-reu programs see "
            "no expansion\n",
            stderr);
    rc = desktop_run(runner, run_output, rom_dir,
                     nforwarded < 0 ? 0 : nforwarded, forwarded);
    if (rc)
      fprintf(stderr, "error: runner exited with status %d\n", rc);
  }
done:
  free(c);
  return rc;
}

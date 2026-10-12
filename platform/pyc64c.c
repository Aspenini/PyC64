#include "desktop.h"

/* pyc64c: the compiler. It turns one entry module (and its imports) into a
   PRG, listing, size report or check result. Projects, running and testing
   live in the pyc64 tool, which invokes this program. */

static void usage(void) {
  puts("PyC64 " VERSION " - native C64 compiler\n"
       "Usage: pyc64c SOURCE [-o OUTPUT] [--emit prg|asm|check|size]\n"
       "              [--target c64|c64-reu|c64-ultimate] [--map FILE]\n"
       "              [-I DIR]... [--asset-dir DIR]\n"
       "       pyc64c --version\n\n"
       "  --emit prg     Write a PRG (default; OUTPUT defaults to SOURCE.PRG)\n"
       "  --emit asm     Write a readable listing (defaults to SOURCE.lst)\n"
       "  --emit check   Check the program without writing output\n"
       "  --emit size    Report code, data and asset sizes\n"
       "  --map FILE     Also write a symbol map\n"
       "  -I DIR         Search DIR for imported modules (repeatable)\n"
       "  --asset-dir    Resolve asset files in DIR\n\n"
       "Use the pyc64 tool for projects (pyc64.toml), new, run, test and "
       "clean.");
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

/* Former pyc64c subcommands now belong to the pyc64 tool. */
static int tool_command(const char *word) {
  const char *commands[] = {"build", "run",  "test", "check", "size", "asm",
                            "new",   "clean", "asset", "sid"};
  size_t i;
  for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
    if (!strcmp(word, commands[i]))
      return 1;
  return 0;
}

int main(int argc, char **argv) {
  Compiler *c;
  const char *source = NULL, *output = NULL, *map = NULL, *machine = NULL;
  const char *emit = "prg", *command = "build";
  char default_output[MAX_PATH];
  int i, rc = 1;
  if (argc == 1) {
    usage();
    return 1;
  }
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
    if (!strcmp(s, "--help") || !strcmp(s, "-h")) {
      usage();
      rc = 0;
      goto done;
    }
    if (!strcmp(s, "-o") || !strcmp(s, "--map") || !strcmp(s, "--target") ||
        !strcmp(s, "-I") || !strcmp(s, "--asset-dir") || !strcmp(s, "--emit")) {
      if (++i == argc || !*argv[i]) {
        fprintf(stderr, "error: %s requires a value\n", s);
        goto done;
      }
      if (!strcmp(s, "-I")) {
        if (c->nsearch_paths == MAX_SEARCH_PATHS ||
            strlen(argv[i]) >= MAX_PATH) {
          fputs("error: too many search paths or search path is too long\n",
                stderr);
          goto done;
        }
        strcpy(c->search_paths[c->nsearch_paths++], argv[i]);
      } else if (!strcmp(s, "--asset-dir")) {
        if (strlen(argv[i]) >= MAX_PATH) {
          fputs("error: asset directory path is too long\n", stderr);
          goto done;
        }
        strcpy(c->asset_root, argv[i]);
      } else if (!strcmp(s, "-o"))
        output = argv[i];
      else if (!strcmp(s, "--map"))
        map = argv[i];
      else if (!strcmp(s, "--target"))
        machine = argv[i];
      else
        emit = argv[i];
    } else if (s[0] == '-') {
      fprintf(stderr, "error: unknown option '%s'\n", s);
      goto done;
    } else if (!source && tool_command(s) && !platform_exists(s)) {
      fprintf(stderr,
              "error: 'pyc64c %s' moved to the project tool: use 'pyc64 %s' "
              "(pyc64c compiles one SOURCE; see --help)\n",
              s, s);
      goto done;
    } else if (source) {
      fputs("error: pyc64c compiles exactly one SOURCE\n", stderr);
      goto done;
    } else
      source = s;
  }
  if (!source) {
    fputs("error: missing SOURCE; see pyc64c --help\n", stderr);
    goto done;
  }
  if (!strcmp(emit, "prg"))
    command = "build";
  else if (!strcmp(emit, "asm") || !strcmp(emit, "check") ||
           !strcmp(emit, "size"))
    command = emit;
  else {
    fprintf(stderr, "error: unknown --emit '%s'; use prg, asm, check or size\n",
            emit);
    goto done;
  }
  if (!output) {
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
  rc = compile_source(c, command, source, output, map);
done:
  free(c);
  return rc;
}

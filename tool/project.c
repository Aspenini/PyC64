#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef MAX_PATH
#endif
#include "tool.h"
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

static int error(const char *path, int line, const char *message) {
  fprintf(stderr, "%s:%d: error: %s\n", path, line, message);
  return 0;
}

static char *space(char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r')
    ++p;
  return p;
}

static int name_valid(const char *name) {
  char upper[64];
  size_t i, length = strlen(name);
  if (!length || length >= sizeof(upper))
    return 0;
  for (i = 0; i < length; ++i) {
    unsigned char ch = (unsigned char)name[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
      return 0;
    upper[i] = (char)toupper(ch);
  }
  upper[length] = 0;
  return strcmp(upper, "CON") && strcmp(upper, "PRN") && strcmp(upper, "AUX") &&
         strcmp(upper, "NUL") &&
         !(length == 4 &&
           (!strncmp(upper, "COM", 3) || !strncmp(upper, "LPT", 3)) &&
           upper[3] >= '1' && upper[3] <= '9');
}

/* A small, explicit TOML schema, not a permissive TOML lookalike. All supported
   values are single-line basic or literal strings. Unknown syntax is rejected.
 */
static int string_value(char *p, char *out, size_t capacity) {
  char quote = *p++;
  size_t n = 0;
  if (quote != '\'' && quote != '"')
    return 0;
  while (*p && *p != quote) {
    unsigned char ch = (unsigned char)*p++;
    if (ch < 32 || ch == 127)
      return 0;
    if (ch == '\\' && quote == '"') {
      ch = (unsigned char)*p++;
      switch (ch) {
      case '"':
      case '\\':
        break;
      case 'b':
        ch = '\b';
        break;
      case 'f':
        ch = '\f';
        break;
      case 'n':
        ch = '\n';
        break;
      case 'r':
        ch = '\r';
        break;
      case 't':
        ch = '\t';
        break;
      default:
        return 0;
      }
    }
    /* These fields are paths/names, so embedded controls have no meaning. */
    if (ch < 32 || n + 1 >= capacity)
      return 0;
    out[n++] = (char)ch;
  }
  if (*p++ != quote)
    return 0;
  out[n] = 0;
  p = space(p);
  return !*p || *p == '#';
}

static int build_directory_valid(char *directory) {
  char *p = directory;
  int components = 0;
  if (!*p || *p == '/' || *p == '\\' || strchr(p, ':'))
    return 0;
  while (*p) {
    char *start = p;
    size_t length;
    while (*p && *p != '/' && *p != '\\')
      ++p;
    length = (size_t)(p - start);
    if (length == 2 && start[0] == '.' && start[1] == '.')
      return 0;
    if (length && !(length == 1 && start[0] == '.')) {
      /* Avoid Windows path aliases and illegal filename characters everywhere.
       */
      size_t i;
      if (start[length - 1] == '.' || start[length - 1] == ' ')
        return 0;
      for (i = 0; i < length; ++i)
        if ((unsigned char)start[i] < 32 || strchr("<>\"|?*", start[i]))
          return 0;
      ++components;
    }
    if (*p)
      *p++ = '/';
  }
  return components != 0;
}

int project_load(Project *project, const char *path) {
  char text[16385], value[MAX_PATH], entry[MAX_PATH] = "main.py";
  const char *tables[] = {"project", "target", "build", "runner", "assets"};
  const char *keys[] = {"name",     "entry",      "machine",  "directory",
                        "optimize", "executable", "directory"};
  const int owners[] = {0, 0, 1, 2, 2, 3, 4};
  unsigned seen = 0, seen_tables = 0;
  int table = -1, line = 0;
  size_t length;
  char *p, *next, *slash;
  FILE *f;
  memset(project, 0, sizeof(*project));
  strcpy(project->directory, "build");
  strcpy(project->machine, "c64");
  if (!desktop_absolute(path, project->file))
    return 0;
  strcpy(project->root, project->file);
  slash = strrchr(project->root, '/');
  if (!slash)
    return error(path, 1, "cannot resolve project directory");
  /* Keep the slash so drive roots and the POSIX root stay absolute. */
  slash[1] = 0;
  f = fopen(project->file, "rb");
  if (!f)
    return error(
        path, 1,
        "cannot open project; use 'pyc64 new NAME' or specify SOURCE");
  length = fread(text, 1, sizeof(text) - 1, f);
  if (ferror(f) || (!feof(f) && fgetc(f) != EOF)) {
    fclose(f);
    return error(path, 1, "cannot read project or project exceeds 16384 bytes");
  }
  fclose(f);
  if (memchr(text, 0, length))
    return error(path, 1, "project contains a NUL byte");
  text[length] = 0;
  for (p = text; p; p = next) {
    char *start, *end;
    int k;
    ++line;
    next = strchr(p, '\n');
    if (next)
      *next++ = 0;
    p = space(p);
    if (!*p || *p == '#')
      continue;
    if (*p == '[') {
      start = ++p;
      end = strchr(p, ']');
      if (!end)
        return error(path, line, "expected a supported TOML table");
      *end++ = 0;
      end = space(end);
      if (*end && *end != '#')
        return error(path, line, "unexpected text after TOML table");
      for (table = 0; table < 5; ++table)
        if (!strcmp(start, tables[table]))
          break;
      if (table == 5 && !strcmp(start, "emulator"))
        return error(path, line,
                     "[emulator] was replaced by [runner]; 'pyc64 run' uses "
                     "pyc64run");
      if (table == 5)
        return error(path, line, "unsupported project table");
      if (seen_tables & (1u << table))
        return error(path, line, "duplicate project table");
      seen_tables |= 1u << table;
      continue;
    }
    start = p;
    while ((*p >= 'a' && *p <= 'z') || *p == '_')
      ++p;
    end = p;
    p = space(p);
    if (*p++ != '=')
      return error(path, line, "expected a bare key and '='");
    *end = 0;
    for (k = 0; k < 7; ++k)
      if (table == owners[k] && !strcmp(start, keys[k]))
        break;
    if (k == 7)
      return error(path, line, "unsupported project key");
    if (seen & (1u << k))
      return error(path, line, "duplicate project key");
    seen |= 1u << k;
    if (!string_value(space(p), value, sizeof(value)))
      return error(path, line,
                   "expected a single-line TOML string (literal paths may use "
                   "single quotes)");
    switch (k) {
    case 0:
      if (!name_valid(value))
        return error(path, line,
                     "project name must be 1-63 letters, digits, '-' or '_', "
                     "excluding reserved device names");
      strcpy(project->name, value);
      break;
    case 1:
      if (!*value)
        return error(path, line, "project entry must not be empty");
      strcpy(entry, value);
      break;
    case 2:
      if (strcmp(value, "c64") && strcmp(value, "c64-reu") &&
          strcmp(value, "c64-ultimate"))
        return error(path, line, "unknown target machine");
      strcpy(project->machine, value);
      break;
    case 3:
      if (!build_directory_valid(value))
        return error(path, line,
                     "build.directory must be a child directory without '..' "
                     "or an absolute path");
      strcpy(project->directory, value);
      break;
    case 4:
      if (strcmp(value, "size"))
        return error(path, line,
                     "only build.optimize = 'size' is currently supported");
      break;
    case 5:
      if (!*value)
        return error(path, line, "runner.executable must not be empty");
      strcpy(project->runner, value);
      break;
    case 6:
      if (!*value)
        return error(path, line, "assets.directory must not be empty");
      if (!desktop_join(project->assets, project->root, value))
        return error(path, line, "asset directory is too long");
      break;
    }
  }
  if (!(seen & 1u))
    return error(path, line, "project.name is required");
  if (!desktop_join(project->entry, project->root, entry))
    return 0;
  /* Bare runner names use PATH; paths are relative to the configuration. */
  if (strchr(project->runner, '/') || strchr(project->runner, '\\')) {
    strcpy(value, project->runner);
    if (!desktop_join(project->runner, project->root, value))
      return 0;
  }
  return 1;
}

int project_artifact(const Project *project, const char *extension, char *out) {
  char directory[MAX_PATH], name[80];
  if (!desktop_join(directory, project->root, project->directory))
    return 0;
  snprintf(name, sizeof(name), "%s%s", project->name, extension);
  if (!desktop_join(out, directory, name))
    return 0;
  if (platform_same_file(out, project->entry) ||
      platform_same_file(out, project->file)) {
    fputs(
        "error: project artifacts must not overwrite source or configuration\n",
        stderr);
    return 0;
  }
  return 1;
}

int project_clean(const Project *project) {
  const char *extensions[] = {".PRG", ".lst", ".map"};
  char paths[3][MAX_PATH];
  int i;
  if (!project_directory(project, 0))
    return 1;
  for (i = 0; i < 3; ++i)
    if (!project_artifact(project, extensions[i], paths[i]))
      return 1;
  for (i = 0; i < 3; ++i)
    if (desktop_directory(paths[i])) {
      fprintf(stderr, "error: project artifact is a directory: '%s'\n",
              paths[i]);
      return 1;
    }
  for (i = 0; i < 3; ++i) {
    if (!remove(paths[i]))
      printf("Removed %s\n", paths[i]);
    else if (errno != ENOENT) {
      fprintf(stderr, "error: cannot remove project artifact '%s'\n", paths[i]);
      return 1;
    }
  }
  return 0;
}

int project_new(const char *name) {
  char root[MAX_PATH], path[MAX_PATH], config[512];
  const char *hello =
      "from c64.video import screen\n\nscreen.clear()\nprint(\"Hello from "
      "PyC64!\")\n";
  const char *ignore = "build/\n__pycache__/\n";
  if (!name_valid(name)) {
    fputs("error: new requires a name of 1-63 letters, digits, '-' or '_', "
          "excluding reserved device names\n",
          stderr);
    return 1;
  }
  if (!desktop_absolute(name, root) || !desktop_mkdir(root))
    return 1;
  snprintf(config, sizeof(config),
           "[project]\nname = \"%s\"\nentry = \"main.py\"\n\n[target]\nmachine "
           "= \"c64\"\n\n[build]\ndirectory = \"build\"\noptimize = \"size\"\n",
           name);
  if (!desktop_join(path, root, "pyc64.toml") ||
      !platform_write(path, (const unsigned char *)config, strlen(config)) ||
      !desktop_join(path, root, "main.py") ||
      !platform_write(path, (const unsigned char *)hello, strlen(hello)) ||
      !desktop_join(path, root, ".gitignore") ||
      !platform_write(path, (const unsigned char *)ignore, strlen(ignore)))
    return 1;
  printf("Created %s\nNext: cd %s, then pyc64 build or pyc64 run\n", root,
         name);
  return 0;
}

static int directory_link(const char *path) {
#ifdef _WIN32
  DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  struct stat s;
  return !lstat(path, &s) && S_ISLNK(s.st_mode);
#endif
}

int project_directory(const Project *project, int create) {
  char path[MAX_PATH], *p;
  size_t base;
  if (!desktop_join(path, project->root, project->directory))
    return 0;
  base = strlen(project->root);
  if (base && project->root[base - 1] != '/')
    ++base;
  for (p = path + base;; ++p) {
    char saved;
    if (*p && *p != '/')
      continue;
    saved = *p;
    *p = 0;
    if (directory_link(path)) {
      fprintf(stderr,
              "error: project build directory must not traverse a link or "
              "junction: '%s'\n",
              path);
      return 0;
    }
    if (!desktop_directory(path)) {
      struct stat s;
      if (!stat(path, &s)) {
        fprintf(stderr,
                "error: build directory component is not a directory: '%s'\n",
                path);
        return 0;
      }
      if (errno != ENOENT) {
        fprintf(stderr, "error: cannot inspect build directory '%s'\n", path);
        return 0;
      }
      if (!create)
        return 1;
      if (!desktop_mkdir(path))
        return 0;
    }
    *p = saved;
    if (!saved)
      return 1;
  }
}

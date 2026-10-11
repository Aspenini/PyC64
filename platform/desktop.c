#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef MAX_PATH
#include <direct.h>
#else
#define _XOPEN_SOURCE 700
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "desktop.h"
#include <errno.h>
#include <sys/stat.h>

int platform_same_file(const char *a, const char *b) {
#ifdef _WIN32
  HANDLE left_file =
      CreateFileA(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  HANDLE right_file =
      CreateFileA(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  BY_HANDLE_FILE_INFORMATION left_info, right_info;
  int same =
      left_file != INVALID_HANDLE_VALUE && right_file != INVALID_HANDLE_VALUE &&
      GetFileInformationByHandle(left_file, &left_info) &&
      GetFileInformationByHandle(right_file, &right_info) &&
      left_info.dwVolumeSerialNumber == right_info.dwVolumeSerialNumber &&
      left_info.nFileIndexHigh == right_info.nFileIndexHigh &&
      left_info.nFileIndexLow == right_info.nFileIndexLow;
  char left[MAX_PATH], right[MAX_PATH];
  if (left_file != INVALID_HANDLE_VALUE)
    CloseHandle(left_file);
  if (right_file != INVALID_HANDLE_VALUE)
    CloseHandle(right_file);
  if (same)
    return 1;
  if (_fullpath(left, a, sizeof(left)) && _fullpath(right, b, sizeof(right)))
    return _stricmp(left, right) == 0;
#else
  struct stat left, right;
  if (stat(a, &left) == 0 && stat(b, &right) == 0)
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
#endif
  return strcmp(a, b) == 0;
}

int platform_read(Compiler *c, const char *path) {
  FILE *f;
  Module *module = &c->modules[c->active_module];
  size_t size, capacity;
  int start = c->source_length;
  if (start >= MAX_SOURCE - 1) {
    fprintf(stderr, "error: combined source arena exhausted\n");
    return 0;
  }
  capacity = (size_t)(MAX_SOURCE - start - 1);
  f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "error: cannot open source '%s'\n", path);
    return 0;
  }
  size = fread(c->source + start, 1, capacity, f);
  if (ferror(f)) {
    fclose(f);
    fprintf(stderr, "error: cannot read source '%s'\n", path);
    return 0;
  }
  if (!feof(f) && fgetc(f) != EOF) {
    fclose(f);
    fprintf(stderr, "error: combined source exceeds %d bytes\n",
            MAX_SOURCE - 1);
    return 0;
  }
  fclose(f);
  c->source[start + size] = 0;
  if (memchr(c->source + start, 0, size)) {
    fprintf(stderr, "error: source contains a NUL byte\n");
    return 0;
  }
  module->start = start;
  module->length = (int)size;
  c->source_length += (int)size + 1;
  return 1;
}
int platform_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}
int platform_write(const char *path, const unsigned char *data, size_t size) {
  FILE *f = fopen(path, "wb");
  int ok;
  if (!f) {
    fprintf(stderr, "error: cannot create '%s'\n", path);
    return 0;
  }
  ok = fwrite(data, 1, size, f) == size;
  if (fclose(f))
    ok = 0;
  if (!ok)
    fprintf(stderr, "error: cannot write '%s'\n", path);
  return ok;
}

static int absolute_path(const char *path) {
  return *path == '/' || *path == '\\' || (path[0] && path[1] == ':');
}

int desktop_join(char *out, const char *root, const char *path) {
  size_t length = strlen(path), base = strlen(root);
  if (absolute_path(path)) {
    if (length >= MAX_PATH)
      goto too_long;
    strcpy(out, path);
  } else {
    int separator = base && root[base - 1] != '/' && root[base - 1] != '\\';
    if (base + length + (size_t)separator >= MAX_PATH)
      goto too_long;
    memcpy(out, root, base);
    if (separator)
      out[base++] = '/';
    memcpy(out + base, path, length + 1);
  }
  return 1;
too_long:
  fputs("error: desktop path is too long\n", stderr);
  return 0;
}

int desktop_absolute(const char *path, char *out) {
  size_t i;
#ifdef _WIN32
  if (!_fullpath(out, path, MAX_PATH)) {
    fputs("error: cannot resolve absolute path\n", stderr);
    return 0;
  }
  for (i = 0; out[i]; ++i)
    if (out[i] == '\\')
      out[i] = '/';
#else
  char buffer[MAX_PATH], cwd[MAX_PATH], *p;
  size_t n = 1, marks[MAX_PATH / 2], depth = 0;
  char *resolved = realpath(path, NULL);
  if (resolved) {
    if (strlen(resolved) >= MAX_PATH) {
      free(resolved);
      fputs("error: desktop path is too long\n", stderr);
      return 0;
    }
    strcpy(out, resolved);
    free(resolved);
    return 1;
  }
  if (!getcwd(cwd, sizeof(cwd)) || !desktop_join(buffer, cwd, path))
    return 0;
  out[0] = '/';
  out[1] = 0;
  for (p = buffer; *p;) {
    char *start;
    while (*p == '/')
      ++p;
    start = p;
    while (*p && *p != '/')
      ++p;
    i = (size_t)(p - start);
    if (!i || (i == 1 && start[0] == '.'))
      continue;
    if (i == 2 && start[0] == '.' && start[1] == '.') {
      if (depth)
        n = marks[--depth];
    } else {
      marks[depth++] = n;
      if (n > 1)
        out[n++] = '/';
      memcpy(out + n, start, i);
      n += i;
    }
    out[n] = 0;
  }
#endif
  return 1;
}

int desktop_directory(const char *path) {
  struct stat s;
  return !stat(path, &s) && (s.st_mode & S_IFMT) == S_IFDIR;
}

int desktop_mkdir(const char *path) {
#ifdef _WIN32
  int rc = _mkdir(path);
#else
  int rc = mkdir(path, 0777);
#endif
  if (rc) {
    fprintf(stderr, "error: cannot create directory '%s': %s\n", path,
            strerror(errno));
    return 0;
  }
  return 1;
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

int desktop_project_directory(const Project *project, int create) {
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

int desktop_launch(const char *executable, const char *prg, int reu) {
  printf("Launching %s\n", executable);
  fflush(stdout);
#ifdef _WIN32
  char command[MAX_PATH * 2 + 80];
  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  DWORD status;
  if (strchr(executable, '"') || strchr(prg, '"') ||
      strlen(executable) >= MAX_PATH || strlen(prg) >= MAX_PATH) {
    fputs("error: invalid emulator executable or PRG path\n", stderr);
    return 1;
  }
  snprintf(command, sizeof(command), "\"%s\"%s -autostart \"%s\"", executable,
           reu ? " -reu -reusize 512" : "", prg);
  memset(&startup, 0, sizeof(startup));
  startup.cb = sizeof(startup);
  if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup,
                      &process)) {
    fprintf(stderr,
            "error: cannot launch VICE '%s' (Windows error %lu); use --vice "
            "PATH or PYC64_VICE\n",
            executable, (unsigned long)GetLastError());
    return 1;
  }
  CloseHandle(process.hThread);
  if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
      !GetExitCodeProcess(process.hProcess, &status)) {
    CloseHandle(process.hProcess);
    fputs("error: cannot wait for VICE\n", stderr);
    return 1;
  }
  CloseHandle(process.hProcess);
  return status <= 255 ? (int)status : 1;
#else
  pid_t child;
  int status;
  child = fork();
  if (child == -1) {
    fputs("error: cannot start VICE process\n", stderr);
    return 1;
  }
  if (!child) {
    char *plain_args[] = {(char *)executable, "-autostart", (char *)prg, NULL};
    char *reu_args[] = {(char *)executable, "-reu",      "-reusize", "512",
                        "-autostart",       (char *)prg, NULL};
    char **args = reu ? reu_args : plain_args;
    execvp(executable, args);
    fprintf(
        stderr,
        "error: cannot launch VICE '%s': %s; use --vice PATH or PYC64_VICE\n",
        executable, strerror(errno));
    _exit(127);
  }
  while (waitpid(child, &status, 0) == -1) {
    if (errno == EINTR)
      continue;
    fputs("error: cannot wait for VICE\n", stderr);
    return 1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
}

void desktop_stdlib(Compiler *c, const char *executable) {
  char root[MAX_PATH], candidate[MAX_PATH], marker[MAX_PATH], *slash;
  int depth;
  if (c->nsearch_paths == MAX_SEARCH_PATHS)
    return;
#ifdef _WIN32
  DWORD length = GetModuleFileNameA(NULL, root, sizeof(root));
  (void)executable;
  if (!length || length >= sizeof(root))
    return;
  for (depth = 0; root[depth]; ++depth)
    if (root[depth] == '\\')
      root[depth] = '/';
#else
  if (strchr(executable, '/')) {
    if (!desktop_absolute(executable, root))
      return;
  } else {
    const char *env = getenv("PATH"), *p;
    int found = 0;
    if (!env)
      return;
    for (p = env;;) {
      const char *end = strchr(p, ':');
      size_t n = end ? (size_t)(end - p) : strlen(p);
      if (n < sizeof(candidate)) {
        memcpy(candidate, p, n);
        candidate[n] = 0;
        if (!n)
          strcpy(candidate, ".");
        if (desktop_join(marker, candidate, executable) &&
            platform_exists(marker) && desktop_absolute(marker, root)) {
          found = 1;
          break;
        }
      }
      if (!end)
        break;
      p = end + 1;
    }
    if (!found)
      return;
  }
#endif
  slash = strrchr(root, '/');
  if (!slash)
    return;
  *slash = 0;
  for (depth = 0; depth < 8; ++depth) {
    const char *locations[] = {"stdlib", "share/pyc64/stdlib"};
    int i;
    for (i = 0; i < 2; ++i) {
      if (!desktop_join(candidate, root, locations[i]) ||
          !desktop_join(marker, candidate, "pyc64/math.py"))
        return;
      if (platform_exists(marker)) {
        strcpy(c->search_paths[c->nsearch_paths++], candidate);
        return;
      }
    }
    slash = strrchr(root, '/');
    if (!slash || slash == root)
      break;
    *slash = 0;
  }
}

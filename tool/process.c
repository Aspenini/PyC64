#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef MAX_PATH
#else
#define _XOPEN_SOURCE 700
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "tool.h"
#include <errno.h>

#ifdef _WIN32
/* Appends one argument using the MSVC runtime's command-line quoting rules. */
static int append_argument(char *out, size_t capacity, size_t *length,
                           const char *argument) {
  size_t n = *length, backslashes = 0, need;
  const char *p;
  /* Worst case: separator, quotes and every character doubled. */
  need = n + 3 + 2 * strlen(argument);
  if (need >= capacity)
    return 0;
  if (n)
    out[n++] = ' ';
  out[n++] = '"';
  for (p = argument; *p; ++p) {
    if (*p == '\\') {
      ++backslashes;
      continue;
    }
    /* Backslashes are literal unless they precede a quote. */
    if (*p == '"')
      backslashes = backslashes * 2 + 1;
    for (; backslashes; --backslashes)
      out[n++] = '\\';
    out[n++] = *p;
  }
  for (backslashes *= 2; backslashes; --backslashes)
    out[n++] = '\\';
  out[n++] = '"';
  out[n] = 0;
  *length = n;
  return 1;
}
#endif

void tool_locate(const char *self, const char *name, const char *environment,
                 char *out) {
  char root[MAX_PATH], file[96];
  const char *override = environment ? getenv(environment) : NULL;
  if (override && *override && strlen(override) < MAX_PATH) {
    strcpy(out, override);
    return;
  }
#ifdef _WIN32
  snprintf(file, sizeof(file), "%s.exe", name);
#else
  snprintf(file, sizeof(file), "%s", name);
#endif
  if (desktop_executable_directory(self, root) && desktop_join(out, root, file) &&
      platform_exists(out))
    return;
  strcpy(out, name);
}

int tool_spawn(const char *const *args, const char *role, const char *hint) {
  fflush(stdout);
  fflush(stderr);
#ifdef _WIN32
  {
    static char command[32768];
    size_t length = 0;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    DWORD status;
    int i;
    command[0] = 0;
    for (i = 0; args[i]; ++i)
      if (!append_argument(command, sizeof(command), &length, args[i])) {
        fprintf(stderr, "error: %s command line is too long\n", role);
        return -1;
      }
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL,
                        &startup, &process)) {
      fprintf(stderr, "error: cannot launch %s '%s' (Windows error %lu); %s\n",
              role, args[0], (unsigned long)GetLastError(), hint);
      return -1;
    }
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &status)) {
      CloseHandle(process.hProcess);
      fprintf(stderr, "error: cannot wait for %s\n", role);
      return -1;
    }
    CloseHandle(process.hProcess);
    return status <= 255 ? (int)status : 1;
  }
#else
  {
    pid_t child = fork();
    int status;
    if (child == -1) {
      fprintf(stderr, "error: cannot start %s process\n", role);
      return -1;
    }
    if (!child) {
      execvp(args[0], (char *const *)args);
      fprintf(stderr, "error: cannot launch %s '%s': %s; %s\n", role, args[0],
              strerror(errno), hint);
      _exit(127);
    }
    while (waitpid(child, &status, 0) == -1) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "error: cannot wait for %s\n", role);
      return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  }
#endif
}

/* Hands the built PRG to pyc64run or pyc64test with forwarded options. */
int tool_launch(const char *program, const char *role, const char *hint,
                  const char *prg, const char *rom_dir, char **forwarded,
                  int nforwarded) {
  const char *args[MAX_TOOL_ARGUMENTS + 6];
  int count = 0, i;
  if (nforwarded > MAX_TOOL_ARGUMENTS) {
    fprintf(stderr, "error: too many %s arguments\n", role);
    return 1;
  }
  args[count++] = program;
  args[count++] = prg;
  if (rom_dir) {
    args[count++] = "--rom-dir";
    args[count++] = rom_dir;
  }
  for (i = 0; i < nforwarded; ++i)
    args[count++] = forwarded[i];
  args[count] = NULL;
  printf("Launching %s\n", program);
  i = tool_spawn(args, role, hint);
  if (i)
    fprintf(stderr, "error: %s exited with status %d\n", role, i < 0 ? 1 : i);
  return i ? 1 : 0;
}

#ifndef PYC64_TOOL_H
#define PYC64_TOOL_H
#include "desktop.h"

/* pyc64: project tool. It reads pyc64.toml and drives pyc64c, pyc64run and
   pyc64test as separate processes, like cargo drives rustc. */

typedef struct {
  char file[MAX_PATH], root[MAX_PATH], name[64], entry[MAX_PATH];
  char directory[MAX_PATH], machine[32], runner[MAX_PATH];
  char assets[MAX_PATH];
} Project;
int project_load(Project *project, const char *path);
int project_new(const char *name);
int project_artifact(const Project *project, const char *extension, char *out);
int project_clean(const Project *project);
/* Checks (and with create, makes) the build directory without following links. */
int project_directory(const Project *project, int create);

#define MAX_TOOL_ARGUMENTS 96
/* Finds a companion program: the ENVIRONMENT override, then the executable
   installed beside this tool, then NAME on PATH. */
void tool_locate(const char *self, const char *name, const char *environment,
                 char *out);
/* Runs args[0] with args (NULL-terminated) and waits. Returns its exit
   status, or -1 when it could not start; ROLE and HINT complete the error. */
int tool_spawn(const char *const *args, const char *role, const char *hint);

/* Hands PRG to pyc64run or pyc64test, adding --rom-dir and forwarded
   options; prints "Launching PROGRAM" and reports a nonzero exit. */
int tool_launch(const char *program, const char *role, const char *hint,
                const char *prg, const char *rom_dir, char **forwarded,
                int nforwarded);

int tool_asset_command(int argc, char **argv);
int tool_sid_command(const char *self, int argc, char **argv);
#endif

#ifndef PYC64_DESKTOP_H
#define PYC64_DESKTOP_H
#include "pyc64.h"

typedef struct {
  char file[MAX_PATH], root[MAX_PATH], name[64], entry[MAX_PATH];
  char directory[MAX_PATH], machine[32], emulator[MAX_PATH];
  char assets[MAX_PATH];
} Project;
int desktop_asset_command(int argc, char **argv);
int desktop_sid_command(int argc, char **argv);

int desktop_absolute(const char *path, char *out);
int desktop_join(char *out, const char *root, const char *path);
int desktop_directory(const char *path);
int desktop_mkdir(const char *path);
int desktop_project_directory(const Project *project, int create);
int desktop_launch(const char *executable, const char *prg, int reu);
void desktop_stdlib(Compiler *c, const char *executable);
int project_load(Project *project, const char *path);
int project_new(const char *name);
int project_artifact(const Project *project, const char *extension, char *out);
int project_clean(const Project *project);
#endif

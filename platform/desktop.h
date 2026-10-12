#ifndef PYC64_DESKTOP_H
#define PYC64_DESKTOP_H
#include "pyc64.h"

/* Desktop host services shared by the pyc64c compiler and the pyc64 tool. */
int desktop_absolute(const char *path, char *out);
int desktop_join(char *out, const char *root, const char *path);
int desktop_directory(const char *path);
int desktop_mkdir(const char *path);
/* Directory of the running executable, with '/' separators. */
int desktop_executable_directory(const char *executable, char *root);
/* Adds the stdlib installed beside (or above) the compiler to search paths. */
void desktop_stdlib(Compiler *c, const char *executable);
#endif

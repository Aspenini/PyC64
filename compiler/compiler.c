#include "pyc64.h"

void fail(Compiler *c, int token, const char *format, ...) {
  va_list args;
  Token *t = &c->tokens[token >= 0 && token < c->ntokens ? token : 0];
  Module *module = &c->modules[t->module];
  int p, end;
  fprintf(stderr, "%s:%d:%d: error: ", *module->path ? module->path : c->path,
          t->line ? t->line : 1, t->column ? t->column : 1);
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputc('\n', stderr);
  if (c->ntokens && t->start < c->source_length) {
    p = t->start;
    while (p > module->start && c->source[p - 1] != '\n')
      --p;
    end = p;
    while (end < module->start + module->length && c->source[end] != '\n')
      ++end;
    fprintf(stderr, "  %.*s\n  %*s^\n", end - p, c->source + p, t->start - p,
            "");
  }
  longjmp(c->abort, 1);
}
int compile_source(Compiler *c, const char *command, const char *source,
                   const char *output, const char *map) {
  int i;
  volatile int rc = 1;
  if (!output) {
    fputs("error: compilation requires an output path\n", stderr);
    return 1;
  }
  if ((output && platform_same_file(source, output)) ||
      (map && platform_same_file(source, map)) ||
      (output && map && platform_same_file(output, map))) {
    fputs("error: source and output paths must be distinct\n", stderr);
    goto done;
  }
  c->path = source;
  if (setjmp(c->abort))
    goto done;
  load_program(c, source);
  prepare_enums(c);
  prepare_classes(c);
  optimize(c);
  infer_signatures(c);
  analyze(c);
  validate_ir(c);
  if (!strcmp(command, "check")) {
    printf("Checked %s\n", source);
    rc = 0;
    goto done;
  }
  if (platform_same_file(source, output) ||
      (map && platform_same_file(output, map))) {
    fputs("error: source and output paths must be distinct\n", stderr);
    goto done;
  }
  for (i = 0; i < c->nmodules; ++i) {
    if (platform_same_file(c->modules[i].path, output) ||
        (map && platform_same_file(c->modules[i].path, map))) {
      fputs("error: outputs must not overwrite an imported source file\n",
            stderr);
      goto done;
    }
  }
  /* Generate and validate layout before opening any output files. */
  for (i = 0; i < c->nassets; ++i)
    if (platform_same_file(c->assets[i].path, output) ||
        (map && platform_same_file(c->assets[i].path, map))) {
      fputs("error: outputs must not overwrite an asset file\n", stderr);
      goto done;
    }
  generate(c);
  if (!strcmp(command, "asm")) {
    c->listing = fopen(output, "w");
    if (!c->listing) {
      fprintf(stderr, "error: cannot create '%s'\n", output);
      goto done;
    }
  }
  if (map) {
    c->map = fopen(map, "w");
    if (!c->map) {
      fprintf(stderr, "error: cannot create '%s'\n", map);
      goto done;
    }
  }
  if (c->listing || c->map) {
    c->image_size = 0;
    generate(c);
  }
  if (!strcmp(command, "build")) {
    unsigned char prg[65538];
    prg[0] = 1;
    prg[1] = 8;
    memcpy(prg + 2, c->image, (size_t)c->image_size);
    if (!platform_write(output, prg, (size_t)c->image_size + 2))
      goto done;
    printf("Wrote %s\n", output);
  }
  printf("Code: %d bytes  Data: %d bytes  Total: %d bytes ($0801-$%04X)\n",
         c->code_size, c->data_size, c->image_size, 0x0800 + c->image_size);
  if (c->asset_size)
    printf("Assets: %d payload bytes (included in Data)\n", c->asset_size);
  rc = 0;
done:
  if (c->listing && fclose(c->listing))
    rc = 1;
  if (c->map && fclose(c->map))
    rc = 1;
  c->listing = c->map = NULL;
  return rc;
}

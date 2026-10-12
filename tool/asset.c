#include "tool.h"

/* pyc64 asset: converts one PNG into a prepacked C64 asset file. */
int tool_asset_command(int argc, char **argv) {
  unsigned char data[MAX_BYTES];
  char output_path[MAX_PATH], error[256];
  const char *source = NULL, *output = NULL,
             *palette = "000000,555555,aaaaaa,ffffff";
  int mode = 0, palette_set = 0, j, count;
  if (argc && (!strcmp(argv[0], "--help") || !strcmp(argv[0], "-h"))) {
    puts(
        "Usage: pyc64 asset "
        "sprite|sprite-multicolor|charset|tileset|tilemap|bitmap|bitmap-screen "
        "SOURCE [-o "
        "OUTPUT]\n"
        "       [--palette RRGGBB,... (4 sprite or 16 bitmap colors)]");
    return 0;
  }
  if (argc)
    mode = !strcmp(argv[0], "sprite")              ? 2
           : !strcmp(argv[0], "sprite-multicolor") ? 3
           : !strcmp(argv[0], "charset")           ? 4
           : !strcmp(argv[0], "tileset")           ? AS_TILESET
           : !strcmp(argv[0], "tilemap")           ? AS_TILEMAP
           : !strcmp(argv[0], "bitmap")            ? AS_BITMAP
           : !strcmp(argv[0], "bitmap-screen")     ? AS_BITMAP_SCREEN
                                                   : 0;
  if (!mode) {
    fputs("error: asset requires sprite, sprite-multicolor, charset, tileset "
          "tilemap, bitmap or bitmap-screen\n",
          stderr);
    return 1;
  }
  for (j = 1; j < argc; ++j) {
    if (!strcmp(argv[j], "-o") || !strcmp(argv[j], "--palette")) {
      int is_palette = !strcmp(argv[j], "--palette");
      if (++j == argc || !*argv[j]) {
        fputs("error: asset option requires a value\n", stderr);
        return 1;
      }
      if (is_palette) {
        palette = argv[j];
        palette_set = 1;
      } else
        output = argv[j];
    } else if (argv[j][0] == '-' || source) {
      fputs(
          "error: asset expects one source and supported conversion options\n",
          stderr);
      return 1;
    } else
      source = argv[j];
  }
  if (!palette_set && mode >= AS_BITMAP)
    palette = "000000,ffffff,68372b,70a4b2,6f3d86,588d43,352879,b8c76f,6f4f25,"
              "433900,9a6759,444444,6c6c6c,9ad284,6c5eb5,959595";
  if (!source || (palette_set && mode != 3 && mode < AS_BITMAP)) {
    fputs("error: asset requires SOURCE; --palette applies only to "
          "sprite-multicolor, bitmap and bitmap-screen\n",
          stderr);
    return 1;
  }
  if (!output) {
    char *dot, *slash, *back;
    if (strlen(source) + 5 >= sizeof(output_path)) {
      fputs("error: asset output path is too long\n", stderr);
      return 1;
    }
    strcpy(output_path, source);
    dot = strrchr(output_path, '.');
    slash = strrchr(output_path, '/');
    back = strrchr(output_path, '\\');
    if (dot && (!slash || dot > slash) && (!back || dot > back))
      *dot = 0;
    strcat(output_path, mode == AS_BITMAP          ? ".hbm"
                        : mode == AS_BITMAP_SCREEN ? ".scr"
                        : mode == AS_TILEMAP       ? ".map"
                        : mode >= AS_CHARSET       ? ".chr"
                                                   : ".spr");
    output = output_path;
  }
  if (platform_same_file(source, output)) {
    fputs("error: asset output must not overwrite its source\n", stderr);
    return 1;
  }
  count = platform_asset(source, mode, palette, data, MAX_ASSET_PAYLOAD, error,
                         sizeof(error));
  if (count < 0) {
    fprintf(stderr, "%s: error: %s\n", source, error);
    return 1;
  }
  if (!platform_write(output, data, (size_t)count))
    return 1;
  if (mode >= AS_BITMAP)
    printf("Wrote %s (%d bytes, hires bitmap %s)\n", output, count,
           mode == AS_BITMAP ? "pixels" : "attributes");
  else if (mode == AS_TILEMAP)
    printf("Wrote %s (%d bytes, %dx%d tile map)\n", output, count,
           data[0] | (data[1] << 8), data[2] | (data[3] << 8));
  else
    printf("Wrote %s (%d bytes, %d %s)\n", output, count,
           count / (mode >= AS_CHARSET ? 8 : 64),
           mode >= AS_CHARSET ? "glyphs" : "sprites");
  return 0;
}

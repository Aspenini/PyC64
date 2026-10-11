#include "pyc64.h"

int asset_kind(const char *name) {
  return !strcmp(name, "pyc64.assets.load")              ? AS_BINARY
         : !strcmp(name, "c64.assets.sprite")            ? AS_SPRITE
         : !strcmp(name, "c64.assets.sprite_multicolor") ? AS_MULTICOLOR
         : !strcmp(name, "c64.assets.charset")           ? AS_CHARSET
         : !strcmp(name, "c64.assets.tileset")           ? AS_TILESET
         : !strcmp(name, "c64.assets.tilemap")           ? AS_TILEMAP
         : !strcmp(name, "c64.assets.bitmap")            ? AS_BITMAP
         : !strcmp(name, "c64.assets.bitmap_screen")     ? AS_BITMAP_SCREEN
         : !strcmp(name, "c64.assets.sid")               ? AS_MUSIC
                                                         : 0;
}

/* Host files become immutable Python bytes; no file reader reaches the 6502. */
void asset_load(Compiler *c, int node, int mode) {
  Node *n = &c->nodes[node];
  unsigned char data[MAX_BYTES];
  char path[MAX_PATH], root[MAX_PATH], error[256];
  const char *file,
      *palette =
          mode == AS_BITMAP || mode == AS_BITMAP_SCREEN
              ? "000000,ffffff,68372b,70a4b2,6f3d86,588d43,352879,b8c76f,"
                "6f4f25,433900,9a6759,444444,6c6c6c,9ad284,6c5eb5,959595"
              : "000000,555555,aaaaaa,ffffff",
      *slash, *back;
  int first = n->b, second = c->nodes[first].next, j, count;
  if (!first || (second && ((mode != AS_MULTICOLOR && mode != AS_BITMAP &&
                             mode != AS_BITMAP_SCREEN) ||
                            c->nodes[second].next)))
    fail(c, n->token, "asset loader expects a path%s",
         mode == AS_MULTICOLOR || mode == AS_BITMAP || mode == AS_BITMAP_SCREEN
             ? " and optional palette"
             : "");
  if (c->nodes[first].kind != N_STRING || !c->nodes[first].d)
    fail(c, n->token, "asset path must be a nonempty compile-time string");
  file = c->strings + c->nodes[first].value;
  if (second) {
    if (c->nodes[second].kind != N_STRING)
      fail(c, n->token, "asset palette must be a compile-time string");
    palette = c->strings + c->nodes[second].value;
  }
  if (strlen(palette) !=
      (mode == AS_BITMAP || mode == AS_BITMAP_SCREEN ? 111u : 27u))
    fail(c, n->token,
         "palette requires %s comma-separated six-digit RGB colors",
         mode == AS_BITMAP || mode == AS_BITMAP_SCREEN ? "sixteen" : "four");
  if (*c->asset_root)
    strcpy(root, c->asset_root);
  else {
    strcpy(root, c->modules[c->tokens[n->token].module].path);
    slash = strrchr(root, '/');
    back = strrchr(root, '\\');
    if (back && (!slash || back > slash))
      slash = back;
    if (slash)
      root[slash - root + 1] = 0;
    else
      strcpy(root, ".");
  }
  if (*file == '/' || *file == '\\' || (file[0] && file[1] == ':')) {
    if (strlen(file) >= sizeof(path))
      fail(c, n->token, "asset path is too long");
    strcpy(path, file);
  } else {
    size_t root_length = strlen(root), file_length = strlen(file);
    if (root_length + file_length + 2 >= sizeof(path))
      fail(c, n->token, "asset path is too long");
    memcpy(path, root, root_length);
    path[root_length] = '/';
    memcpy(path + root_length + 1, file, file_length + 1);
  }
  for (j = 0; j < c->nassets; ++j)
    if (c->assets[j].mode == mode && !strcmp(c->assets[j].palette, palette) &&
        platform_same_file(c->assets[j].path, path))
      break;
  if (j == c->nassets) {
    Asset *asset;
    if (j == MAX_ASSETS)
      fail(c, n->token, "asset arena exhausted (limit %d)", MAX_ASSETS);
    count = platform_asset(path, mode, palette, data, MAX_ASSET_PAYLOAD, error,
                           sizeof(error));
    if (count < 0)
      fail(c, n->token, "asset '%s': %s", path, error);
    bytes_literal(c, node, data, count);
    asset = &c->assets[c->nassets++];
    strcpy(asset->path, path);
    strcpy(asset->palette, palette);
    asset->mode = mode;
    asset->offset = (int)n->value;
    asset->count = count;
  } else {
    Asset *asset = &c->assets[j];
    bytes_literal(c, node, c->bytes + asset->offset, asset->count);
  }
  if (mode >= AS_SPRITE && mode <= AS_TILESET) {
    int alignment = mode >= AS_CHARSET ? 2048 : 64;
    if (c->byte_alignment[n->value] < alignment)
      c->byte_alignment[n->value] = (unsigned short)alignment;
    c->byte_vic[n->value] = 1;
  }
  if (mode == AS_BITMAP) {
    c->byte_alignment[n->value] = 8192;
    c->byte_vic[n->value] = 1;
  }
  n->c = 1; /* A call remains valid as an expression statement. */
}

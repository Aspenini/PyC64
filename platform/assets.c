#include "desktop.h"
#include <ctype.h>

/* Image decoding is a desktop service. Prepacked SPR/CHR/MAP/HBM/SCR only need
   the bounded reader, which the planned cc65 host can implement independently.
 */
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 2048
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100 4244 4267 4456)
#endif
#include "vendor/stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static int asset_error(char *out, size_t size, const char *message) {
  snprintf(out, size, "%s", message);
  return -1;
}
static int suffix(const char *path, const char *extension) {
  size_t a = strlen(path), b = strlen(extension), j;
  if (a < b)
    return 0;
  for (j = 0; j < b; ++j)
    if (tolower((unsigned char)path[a - b + j]) != extension[j])
      return 0;
  return 1;
}
static int raw_asset(const char *path, unsigned char *data, int capacity,
                     char *error, size_t size) {
  FILE *f = fopen(path, "rb");
  size_t count;
  int failed, extra;
  if (!f)
    return asset_error(error, size, "cannot open asset file");
  count = fread(data, 1, (size_t)capacity, f);
  extra = fgetc(f);
  failed = ferror(f);
  fclose(f);
  if (failed)
    return asset_error(error, size, "cannot read asset file");
  if (extra != EOF) {
    snprintf(error, size, "asset exceeds the %d-byte payload limit", capacity);
    return -1;
  }
  return (int)count;
}
static int hex(int ch) {
  return ch >= '0' && ch <= '9'   ? ch - '0'
         : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
         : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                  : -1;
}
static int colors(const char *palette, int rgb[][3], int count) {
  int j, k, a, b;
  if (strlen(palette) != (size_t)(count * 7 - 1))
    return 0;
  for (j = 0; j < count; ++j) {
    if (j < count - 1 && palette[j * 7 + 6] != ',')
      return 0;
    for (k = 0; k < 3; ++k) {
      a = hex((unsigned char)palette[j * 7 + k * 2]);
      b = hex((unsigned char)palette[j * 7 + k * 2 + 1]);
      if (a < 0 || b < 0)
        return 0;
      rgb[j][k] = a * 16 + b;
    }
  }
  return 1;
}
static uint32_t big(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}
/* stb intentionally ignores PNG CRCs. Validate the complete chunk stream before
   decoding so damaged/truncated sources never silently become shipped assets.
 */
static int png_valid(const unsigned char *data, size_t size) {
  static const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
  size_t p = 8, j;
  int first = 1, k;
  if (size < 8 || memcmp(data, signature, 8))
    return 0;
  while (size - p >= 12) {
    uint32_t count = big(data + p), crc = UINT32_C(0xffffffff);
    if (count > size - p - 12)
      return 0;
    if (first && (count != 13 || memcmp(data + p + 4, "IHDR", 4)))
      return 0;
    first = 0;
    for (j = p + 4; j < p + 8 + count; ++j) {
      crc ^= data[j];
      for (k = 0; k < 8; ++k)
        crc = (crc >> 1) ^ (crc & 1 ? UINT32_C(0xedb88320) : 0);
    }
    if ((crc ^ UINT32_C(0xffffffff)) != big(data + p + 8 + count))
      return 0;
    if (!memcmp(data + p + 4, "IEND", 4))
      return count == 0 && p + 12 == size;
    p += (size_t)count + 12;
  }
  return 0;
}
static int pixel(const unsigned char *rgba, int mode, int rgb[4][3]) {
  int j, k, best = 0, distance = 0x7fffffff;
  if (rgba[3] < 128)
    return 0;
  if (mode != 3)
    return (rgba[0] | rgba[1] | rgba[2]) != 0;
  for (j = 0; j < 4; ++j) {
    int current = 0;
    for (k = 0; k < 3; ++k) {
      int d = rgba[k] - rgb[j][k];
      current += d * d;
    }
    if (current < distance) {
      distance = current;
      best = j;
    }
  }
  return best;
}
static int map_valid(const unsigned char *data, int count) {
  uint32_t width, height;
  if (count < 5)
    return 0;
  width = data[0] | (data[1] << 8);
  height = data[2] | (data[3] << 8);
  return width && width <= 256 && height && height <= 256 &&
         width * height + 4 == (uint32_t)count;
}
static int color_distance(const unsigned char *p, const int *rgb) {
  int k, distance = 0;
  for (k = 0; k < 3; ++k) {
    int d = p[k] - rgb[k];
    distance += d * d;
  }
  return distance;
}
/* Two colors per 8x8 cell: dominant palette color is background, then the
   most frequent remaining color is foreground. Ties use lower palette indices.
   Requantize the original pixels to that pair, with ties to background. */
static int extract_bitmap(const unsigned char *rgba, int mode, int rgb[16][3],
                          unsigned char *data) {
  int tx, ty, x, y, j, cell = 0;
  for (ty = 0; ty < 200; ty += 8)
    for (tx = 0; tx < 320; tx += 8, ++cell) {
      int counts[16] = {0}, background = 0, foreground;
      for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x) {
          const unsigned char *p = rgba + ((ty + y) * 320 + tx + x) * 4;
          int best = 0, distance = 0x7fffffff;
          if (p[3] >= 128)
            for (j = 0; j < 16; ++j) {
              int d = color_distance(p, rgb[j]);
              if (d < distance) {
                best = j;
                distance = d;
              }
            }
          ++counts[best];
        }
      for (j = 1; j < 16; ++j)
        if (counts[j] > counts[background])
          background = j;
      foreground = background == 0 ? 1 : 0;
      for (j = 0; j < 16; ++j)
        if (j != background && counts[j] > counts[foreground])
          foreground = j;
      if (mode == AS_BITMAP_SCREEN) {
        data[cell] = (unsigned char)((foreground << 4) | background);
        continue;
      }
      for (y = 0; y < 8; ++y) {
        int bits = 0;
        for (x = 0; x < 8; ++x) {
          const unsigned char *p = rgba + ((ty + y) * 320 + tx + x) * 4;
          unsigned char transparent[3];
          if (p[3] < 128) {
            for (j = 0; j < 3; ++j)
              transparent[j] = (unsigned char)rgb[0][j];
            p = transparent;
          }
          if (color_distance(p, rgb[foreground]) <
              color_distance(p, rgb[background]))
            bits |= 1 << (7 - x);
        }
        data[cell * 8 + y] = (unsigned char)bits;
      }
    }
  return mode == AS_BITMAP ? 8000 : 1000;
}
static int extract_tiles(const unsigned char *rgba, int width, int height,
                         int mode, unsigned char *data, int capacity,
                         char *error, size_t size) {
  unsigned char glyphs[2048], tile[8];
  int tx, ty, x, y, unique = 0, index = 4, j;
  int columns = width / 8, rows = height / 8;
  if (mode == AS_TILEMAP && columns * rows + 4 > capacity)
    return asset_error(error, size,
                       "tile map exceeds the 32764-byte payload limit");
  for (ty = 0; ty < height; ty += 8)
    for (tx = 0; tx < width; tx += 8) {
      memset(tile, 0, sizeof(tile));
      for (y = 0; y < 8; ++y)
        for (x = 0; x < 8; ++x) {
          const unsigned char *p = rgba + ((ty + y) * width + tx + x) * 4;
          if (p[3] >= 128 && (p[0] | p[1] | p[2]))
            tile[y] |= (unsigned char)(1 << (7 - x));
        }
      for (j = 0; j < unique; ++j)
        if (!memcmp(glyphs + j * 8, tile, 8))
          break;
      if (j == unique) {
        if (unique == 256)
          return asset_error(error, size,
                             "tile image exceeds 256 distinct glyphs");
        memcpy(glyphs + unique * 8, tile, 8);
        ++unique;
      }
      if (mode == AS_TILEMAP)
        data[index++] = (unsigned char)j;
    }
  if (mode == AS_TILEMAP) {
    data[0] = (unsigned char)columns;
    data[1] = (unsigned char)(columns >> 8);
    data[2] = (unsigned char)rows;
    data[3] = (unsigned char)(rows >> 8);
    return index;
  }
  if (unique * 8 > capacity)
    return asset_error(error, size, "tileset exceeds the asset payload limit");
  memcpy(data, glyphs, (size_t)unique * 8);
  return unique * 8;
}
int platform_asset(const char *path, int mode, const char *palette,
                   unsigned char *data, int capacity, char *error,
                   size_t size) {
  enum { INPUT_LIMIT = 16 * 1024 * 1024 };
  unsigned char *encoded, *rgba;
  FILE *f;
  size_t length;
  int width, height, channels, rgb[16][3],
      tile_width = mode == 3            ? 12
                   : mode >= AS_CHARSET ? 8
                                        : 24;
  int tile_height = mode >= AS_CHARSET ? 8 : 21,
      stride = mode >= AS_CHARSET ? 8 : 64;
  int count, x, y, tx, ty, index = 0;
  if (mode == AS_MUSIC) {
    SidInfo info;
    count = raw_asset(path, data, capacity, error, size);
    if (count < 0 || !sid_info(data, count, &info, error, size))
      return -1;
    return count;
  }
  if ((mode == AS_MULTICOLOR || mode >= AS_BITMAP) &&
      !colors(palette, rgb, mode >= AS_BITMAP ? 16 : 4))
    return asset_error(
        error, size,
        mode >= AS_BITMAP
            ? "palette requires sixteen comma-separated six-digit RGB colors"
            : "palette requires four comma-separated six-digit RGB colors");
  if (mode == AS_BINARY ||
      ((mode == AS_SPRITE || mode == AS_MULTICOLOR) && suffix(path, ".spr")) ||
      ((mode == AS_CHARSET || mode == AS_TILESET) && suffix(path, ".chr")) ||
      (mode == AS_TILEMAP && suffix(path, ".map")) ||
      (mode == AS_BITMAP && suffix(path, ".hbm")) ||
      (mode == AS_BITMAP_SCREEN && suffix(path, ".scr"))) {
    count = raw_asset(path, data, capacity, error, size);
    if (count < 0 || mode == 1)
      return count;
    if (mode >= AS_BITMAP) {
      if (count != (mode == AS_BITMAP ? 8000 : 1000))
        return asset_error(error, size,
                           "HBM requires 8000 bytes; SCR requires 1000 bytes");
      return count;
    }
    if (mode == AS_TILEMAP) {
      if (!map_valid(data, count))
        return asset_error(error, size,
                           "MAP requires 1..256 columns/rows and exactly width "
                           "* height tile indices");
      return count;
    }
    if (!count || count % stride || (mode >= AS_CHARSET && count > 2048))
      return asset_error(error, size,
                         mode >= AS_CHARSET
                             ? "CHR requires 1..256 eight-byte glyphs"
                             : "SPR requires complete 64-byte sprite records");
    return count;
  }
  encoded = (unsigned char *)malloc(INPUT_LIMIT);
  if (!encoded)
    return asset_error(error, size, "cannot allocate PNG input buffer");
  f = fopen(path, "rb");
  if (!f) {
    free(encoded);
    return asset_error(error, size, "cannot open asset file");
  }
  length = fread(encoded, 1, INPUT_LIMIT, f);
  count = ferror(f) || fgetc(f) != EOF;
  fclose(f);
  if (count || !png_valid(encoded, length)) {
    free(encoded);
    return asset_error(error, size,
                       "invalid PNG chunks/CRC or input exceeds 16 MiB");
  }
  rgba = stbi_load_from_memory(encoded, (int)length, &width, &height, &channels,
                               4);
  free(encoded);
  if (!rgba)
    return asset_error(error, size,
                       "cannot decode PNG (dimensions limited to 2048)");
  if (mode >= AS_BITMAP) {
    if (width != 320 || height != 200 ||
        capacity < (mode == AS_BITMAP ? 8000 : 1000)) {
      stbi_image_free(rgba);
      return asset_error(
          error, size,
          "hires bitmap PNG must be 320x200 with sufficient payload capacity");
    }
    count = extract_bitmap(rgba, mode, rgb, data);
    stbi_image_free(rgba);
    return count;
  }
  if (width % tile_width || height % tile_height) {
    stbi_image_free(rgba);
    snprintf(error, size, "PNG dimensions must be multiples of %dx%d",
             tile_width, tile_height);
    return -1;
  }
  if (mode == AS_TILESET || mode == AS_TILEMAP) {
    count =
        extract_tiles(rgba, width, height, mode, data, capacity, error, size);
    stbi_image_free(rgba);
    return count;
  }
  count = (width / tile_width) * (height / tile_height) * stride;
  if (count > capacity || (mode == 4 && count > 2048)) {
    stbi_image_free(rgba);
    return asset_error(
        error, size,
        mode == 4 ? "charset exceeds 256 glyphs"
                  : "converted asset exceeds the 32764-byte payload limit");
  }
  memset(data, 0, (size_t)count);
  for (ty = 0; ty < height; ty += tile_height)
    for (tx = 0; tx < width; tx += tile_width) {
      for (y = 0; y < tile_height; ++y)
        for (x = 0; x < tile_width; ++x) {
          const unsigned char *p = rgba + ((ty + y) * width + tx + x) * 4;
          int bits = mode == 3 ? 2 : 1, position = x * bits;
          data[index + y * (tile_width * bits / 8) + position / 8] |=
              (unsigned char)(pixel(p, mode, rgb) << (8 - bits - position % 8));
        }
      index += stride;
    }
  stbi_image_free(rgba);
  return count;
}

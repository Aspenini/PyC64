#include "pyc64.h"

static int sid_error(char *error, size_t size, const char *message) {
  snprintf(error, size, "%s", message);
  return 0;
}
static int sid_word(const unsigned char *data) {
  return (data[0] << 8) | data[1];
}
/* Executable SID assets reserve their native RAM. PSID uses init/play calls;
   the RSID library profile requires a returning init and owns its IRQ source.
 */
static int sid_parse(const unsigned char *data, int count, SidInfo *info,
                     char *error, size_t size, int library) {
  int version, header, end, rsid = count >= 4 && !memcmp(data, "RSID", 4);
  if (count < 118 || (!rsid && memcmp(data, "PSID", 4)))
    return sid_error(error, size, "SID requires a complete PSID/RSID header");
  version = sid_word(data + 4);
  if (version < (rsid ? 2 : 1) || version > 4)
    return sid_error(error, size,
                     rsid ? "RSID version must be 2..4"
                          : "PSID version must be 1..4");
  if (rsid && (sid_word(data + 8) || sid_word(data + 12) || data[18] ||
               data[19] || data[20] || data[21]))
    return sid_error(error, size,
                     "RSID reserved load/play/speed fields must be zero");
  header = version == 1 ? 118 : 124;
  if (count <= header || sid_word(data + 6) != header)
    return sid_error(error, size,
                     "PSID has an invalid data offset or empty payload");
  memset(info, 0, sizeof(*info));
  info->version = version;
  info->rsid = rsid;
  info->speed = ((uint32_t)data[18] << 24) | ((uint32_t)data[19] << 16) |
                ((uint32_t)data[20] << 8) | data[21];
  info->offset = header;
  info->load = sid_word(data + 8);
  if (!info->load) {
    if (count < header + 3)
      return sid_error(error, size, "PSID payload is missing its load address");
    info->load = data[header] | (data[header + 1] << 8);
    info->offset += 2;
  }
  info->length = count - info->offset;
  info->init = sid_word(data + 10);
  if (!info->init)
    info->init = info->load;
  info->play = sid_word(data + 12);
  info->songs = sid_word(data + 14);
  info->start = sid_word(data + 16);
  if (!info->songs || info->songs > 256 || !info->start ||
      info->start > info->songs)
    return sid_error(error, size,
                     "PSID songs/start song must be within 1..256");
  if (version >= 2) {
    info->flags = sid_word(data + 118);
    if (library && rsid && (data[120] || data[121]))
      return sid_error(error, size,
                       "RSID library playback requires a clean image with zero "
                       "relocation fields");
    if (library && rsid && (info->flags & 2))
      return sid_error(
          error, size,
          "RSID BASIC launches need a standalone execution environment");
    if (info->flags & ~0x3ff || info->flags & (rsid && !library ? 1 : 3))
      return sid_error(
          error, size,
          "PSID MUS/PlaySID-specific or reserved flags are unsupported");
    if (data[122] || data[123] || (info->flags & 0x3c0))
      return sid_error(error, size,
                       "PSID playback requires one SID chip at $D400");
  }
  end = info->load + info->length;
  if (!library && rsid) {
    int first = data[120], pages = data[121], p;
    if (info->load < 0x07e8 || end > 0x10000 || info->length <= 0)
      return sid_error(error, size,
                       "RSID load image must fit RAM $07E8..$FFFF");
    if (info->flags & 2) {
      if (sid_word(data + 10))
        return sid_error(error, size, "RSID BASIC init address must be zero");
      if (info->load > 0x0801 || end <= 0x0803)
        return sid_error(error, size,
                         "RSID BASIC image must contain the program at $0801");
      info->init = 0;
    } else if (info->init < info->load || info->init >= end ||
               !((info->init >= 0x07e8 && info->init < 0xa000) ||
                 (info->init >= 0xc000 && info->init < 0xd000)))
      return sid_error(error, size,
                       "RSID init must be inside its image in ROM-visible RAM");
    if ((!first || first == 255) && pages)
      return sid_error(
          error, size,
          "RSID relocation page count must be zero for start 0 or 255");
    if (first && first != 255) {
      if (!pages || first + pages > 256)
        return sid_error(error, size,
                         "RSID relocation range is empty or overflows RAM");
      for (p = first; p < first + pages; ++p)
        if (p < 4 || (p >= 0xa0 && p < 0xc0) || p >= 0xd0 ||
            ((p << 8) < end && ((p + 1) << 8) > info->load))
          return sid_error(error, size,
                           "RSID relocation range overlaps reserved RAM, ROM "
                           "or its load image");
    }
    return 1;
  }
  if (!info->length || !((info->load >= 0x0810 && end <= 0xa000) ||
                         (info->load >= 0xc000 && end <= 0xd000)))
    return sid_error(error, size,
                     "PSID payload must fit RAM $0810..$9FFF or $C000..$CFFF");
  if (info->init < info->load || info->init >= end ||
      (!rsid && (info->play < info->load || info->play >= end)))
    return sid_error(
        error, size,
        rsid ? "RSID requires a returning machine-code init inside its payload"
             : "PSID requires init/play RTS routines inside its payload");
  return 1;
}
int sid_info(const unsigned char *data, int count, SidInfo *info, char *error,
             size_t size) {
  return sid_parse(data, count, info, error, size, 1);
}
int rsid_info(const unsigned char *data, int count, SidInfo *info, char *error,
              size_t size) {
  if (count < 4 || memcmp(data, "RSID", 4))
    return sid_error(error, size,
                     "standalone sid launch requires an RSID file");
  return sid_parse(data, count, info, error, size, 0);
}

#include "desktop.h"

/* A standalone launcher owns the machine. Its resident page is outside the
   tune's image; after handoff there is no IRQ/NMI trampoline or Python frame.
 */
typedef struct {
  unsigned char bytes[256];
  int size, address, failed;
} SidCode;
static void emit(SidCode *c, int op, int operand, int length) {
  if (c->size + length > 256) {
    c->failed = 1;
    return;
  }
  c->bytes[c->size++] = (unsigned char)op;
  if (length > 1)
    c->bytes[c->size++] = (unsigned char)operand;
  if (length > 2)
    c->bytes[c->size++] = (unsigned char)(operand >> 8);
}
static void store(SidCode *c, int address, int value) {
  emit(c, 0xa9, value, 2);
  emit(c, address < 256 ? 0x85 : 0x8d, address, address < 256 ? 2 : 3);
}
static void branch(SidCode *c, int op, int target) {
  emit(c, op, target - c->size - 2, 2);
}
static int free_page(const unsigned char *data, const SidInfo *sid) {
  int first = data[120], pages = data[121], p, end = sid->load + sid->length;
  if (first == 255)
    return -1;
  for (p = first ? first : 4; p < (first ? first + pages : 0xd0); ++p)
    if (!(p >= 0xa0 && p < 0xc0) &&
        !((p << 8) < end && ((p + 1) << 8) > sid->load))
      return p << 8;
  return -1;
}
static int basic_end(const unsigned char *payload, const SidInfo *sid) {
  int address = 0x0801, end = sid->load + sid->length;
  while (address + 2 <= end && address < 0xa000) {
    int offset = address - sid->load;
    int next = payload[offset] | (payload[offset + 1] << 8);
    if (!next)
      return address + 2;
    if (next <= address + 4 || next + 2 > end || next >= 0xa000 ||
        payload[next - sid->load - 1])
      return -1;
    address = next;
  }
  return -1;
}
static void resident(SidCode *c, const SidInfo *sid, int source, int song,
                     int basic) {
  int backward = sid->load > source && sid->load < source + sid->length;
  int j, loop, skip, pal, done;
  /* Remove pending interrupt sources before mapping RAM under ROM and I/O. */
  store(c, 0xdc0d, 127);
  emit(c, 0x8d, 0xdd0d, 3);
  emit(c, 0xad, 0xdc0d, 3);
  emit(c, 0xad, 0xdd0d, 3);
  store(c, 0xd01a, 0);
  emit(c, 0x8d, 0xdc0e, 3);
  emit(c, 0x8d, 0xdc0f, 3);
  emit(c, 0x8d, 0xdd0e, 3);
  emit(c, 0x8d, 0xdd0f, 3);
  store(c, 0xd019, 15);
  /* Discard the temporary page-two installer, keeping the ROM-created state. */
  emit(c, 0xa2, 13, 2);
  emit(c, 0xa9, 0, 2);
  loop = c->size;
  emit(c, 0x9d, 0x0200, 3);
  emit(c, 0xca, 0, 1);
  branch(c, 0x10, loop);
  store(c, 1, 0x34);
  for (j = 0; j < 2; ++j) {
    int from = source + (backward ? sid->length - 1 : 0);
    int to = sid->load + (backward ? sid->length - 1 : 0);
    store(c, 0xfb + j, (from >> (j * 8)) & 255);
    store(c, 0xfd + j, (to >> (j * 8)) & 255);
    store(c, 0xf9 + j, (sid->length >> (j * 8)) & 255);
  }
  emit(c, 0xa0, 0, 2);
  loop = c->size;
  emit(c, 0xb1, 0xfb, 2);
  emit(c, 0x91, 0xfd, 2);
  for (j = 0; j < 2; ++j) {
    int pointer = 0xfb + j * 2;
    if (backward) {
      emit(c, 0xa5, pointer, 2);
      skip = c->size;
      emit(c, 0xd0, 2, 2);
      emit(c, 0xc6, pointer + 1, 2);
      emit(c, 0xc6, pointer, 2);
      (void)skip;
    } else {
      emit(c, 0xe6, pointer, 2);
      emit(c, 0xd0, 2, 2);
      emit(c, 0xe6, pointer + 1, 2);
    }
  }
  emit(c, 0xa5, 0xf9, 2);
  emit(c, 0xd0, 2, 2);
  emit(c, 0xc6, 0xfa, 2);
  emit(c, 0xc6, 0xf9, 2);
  emit(c, 0xa5, 0xf9, 2);
  emit(c, 0x05, 0xfa, 2);
  branch(c, 0xd0, loop);
  store(c, 1, 0x37);
  store(c, 0xdc06, 255);
  emit(c, 0x8d, 0xdc07, 3);
  for (j = 4; j <= 7; ++j)
    emit(c, 0x8d, 0xdd00 + j, 3);
  store(c, 0xd011, 0x9b);
  store(c, 0xd012, 0x37);
  emit(c, 0xad, 0x02a6, 3);
  pal = c->size;
  emit(c, 0xf0, 0, 2);
  store(c, 0xdc04, 0x25);
  store(c, 0xdc05, 0x40);
  done = c->size;
  emit(c, 0x4c, 0, 3);
  c->bytes[pal + 1] = (unsigned char)(c->size - pal - 2);
  store(c, 0xdc04, 0x95);
  store(c, 0xdc05, 0x42);
  c->bytes[done + 1] = (unsigned char)(c->address + c->size);
  c->bytes[done + 2] = (unsigned char)((c->address + c->size) >> 8);
  store(c, 0xdc0e, 0x11);
  store(c, 0xdc0d, 0x81);
  store(c, 0xd418, 15);
  emit(c, 0xa2, 5, 2);
  emit(c, 0xa9, 0, 2);
  loop = c->size;
  emit(c, 0x95, 0xf9, 2);
  emit(c, 0xca, 0, 1);
  branch(c, 0x10, loop);
  if (sid->flags & 2) {
    store(c, 0x2d, basic & 255);
    store(c, 0x2e, basic >> 8);
    store(c, 0x030c, song - 1);
    emit(c, 0x58, 0, 1);
    emit(c, 0xa9, 0, 2);
    emit(c, 0x20, 0xa871,
         3); /* RUN clears variables and returns through a real frame. */
    emit(c, 0x4c, 0xa7ae, 3);
  } else {
    emit(c, 0xa2, 0, 2);
    emit(c, 0xa0, 0, 2);
    emit(c, 0xa9, song - 1, 2);
    emit(c, 0x58, 0, 1);
    emit(c, 0x20, sid->init, 3);
    loop = c->address + c->size;
    emit(c, 0x4c, loop, 3);
  }
}
int desktop_sid_command(int argc, char **argv) {
  const char *source = NULL, *output = NULL, *mapping = NULL, *vice = NULL;
  char path[MAX_PATH], absolute[MAX_PATH], error[256];
  unsigned char *data = NULL, *image = NULL;
  SidInfo sid;
  SidCode boot = {{0}, 0, 0x080d, 0}, driver = {{0}, 0, 0, 0};
  FILE *file = NULL;
  int i, count, selected = 0, run = 0, rc = 1, stage = 0x0a00, end, basic = 0,
                ram_byte;
  for (i = 0; i < argc; ++i) {
    const char *s = argv[i];
    if (!strcmp(s, "--run")) {
      run = 1;
      continue;
    }
    if (!strcmp(s, "-o") || !strcmp(s, "--map") || !strcmp(s, "--song") ||
        !strcmp(s, "--vice")) {
      if (++i == argc || !*argv[i]) {
        fprintf(stderr, "error: %s requires a value\n", s);
        goto done;
      }
      if (!strcmp(s, "-o"))
        output = argv[i];
      else if (!strcmp(s, "--map"))
        mapping = argv[i];
      else if (!strcmp(s, "--vice"))
        vice = argv[i];
      else {
        const char *p = argv[i];
        selected = 0;
        while (*p) {
          if (*p < '0' || *p > '9' ||
              (selected = selected * 10 + *p - '0') > 256) {
            fputs("error: --song must be 0..256\n", stderr);
            goto done;
          }
          ++p;
        }
      }
    } else if (*s == '-' || source) {
      fprintf(stderr, "error: unexpected sid argument '%s'\n", s);
      goto done;
    } else
      source = s;
  }
  if (!source || (vice && !run)) {
    fputs("error: use pyc64c sid FILE.sid [-o FILE.PRG] [--song N] [--map "
          "FILE] [--run --vice PATH]\n",
          stderr);
    goto done;
  }
  if (!output) {
    char *dot, *slash, *back;
    if (strlen(source) + 5 >= sizeof(path)) {
      fputs("error: SID path is too long\n", stderr);
      goto done;
    }
    strcpy(path, source);
    dot = strrchr(path, '.');
    slash = strrchr(path, '/');
    back = strrchr(path, '\\');
    if (dot && (!slash || dot > slash) && (!back || dot > back))
      *dot = 0;
    strcat(path, ".PRG");
    output = path;
  }
  if (platform_same_file(source, output) ||
      (mapping && (platform_same_file(source, mapping) ||
                   platform_same_file(output, mapping)))) {
    fputs("error: SID input, PRG and map paths must be distinct\n", stderr);
    goto done;
  }
  data = (unsigned char *)malloc(65537);
  image = (unsigned char *)calloc(65538, 1);
  if (!data || !image) {
    fputs("error: unable to acquire SID launch arena\n", stderr);
    goto done;
  }
  file = fopen(source, "rb");
  if (!file) {
    fprintf(stderr, "error: cannot read '%s'\n", source);
    goto done;
  }
  count = (int)fread(data, 1, 65537, file);
  if (ferror(file) || count > 65536) {
    fputs("error: SID input exceeds 65536 bytes or could not be read\n",
          stderr);
    goto done;
  }
  fclose(file);
  file = NULL;
  if (!rsid_info(data, count, &sid, error, sizeof(error))) {
    fprintf(stderr, "error: %s\n", error);
    goto done;
  }
  if (!selected)
    selected = sid.start;
  if (selected > sid.songs) {
    fputs("error: selected SID song does not exist\n", stderr);
    goto done;
  }
  if ((driver.address = free_page(data, &sid)) < 0) {
    fputs("error: RSID has no declared free RAM page for its standalone "
          "launcher\n",
          stderr);
    goto done;
  }
  if (stage < driver.address + 256 && stage + sid.length > driver.address)
    stage = driver.address + 256;
  end = stage + sid.length;
  if (end > 0xd000) {
    fputs("error: RSID payload cannot be staged below I/O in one PRG\n",
          stderr);
    goto done;
  }
  if ((sid.flags & 2) && (basic = basic_end(data + sid.offset, &sid)) < 0) {
    fputs("error: malformed RSID BASIC line links or terminators\n", stderr);
    goto done;
  }
  resident(&driver, &sid, stage, selected, basic);
  /* Use standard C64 ROM entry points to recreate RAM, I/O and BASIC state.
     RAMTAS preserves tested RAM except its failed probe at $A000, which is
     restored from the staged image's original byte before launch. */
  emit(&boot, 0x78, 0, 1);
  emit(&boot, 0xd8, 0, 1);
  emit(&boot, 0xa2, 255, 2);
  emit(&boot, 0x9a, 0, 1);
  store(&boot, 0, 0x2f);
  store(&boot, 1, 0x37);
  emit(&boot, 0xad, 0x02a6, 3);
  emit(&boot, 0x8d, 0x08ff, 3);
  emit(&boot, 0x20, 0xff87, 3);
  emit(&boot, 0x20, 0xff8a, 3);
  emit(&boot, 0x20, 0xff84, 3);
  emit(&boot, 0x20, 0xff81, 3);
  emit(&boot, 0x20, 0xe453, 3);
  emit(&boot, 0x20, 0xe3bf, 3);
  emit(&boot, 0x20, 0xa644,
       3); /* BASIC NEW establishes the empty-program pointers. */
  emit(&boot, 0x78, 0, 1);
  emit(&boot, 0xad, 0x08ff, 3);
  emit(&boot, 0x8d, 0x02a6, 3);
  ram_byte =
      stage <= 0xa000 && end > 0xa000 ? data[sid.offset + 0xa000 - stage] : 0;
  store(&boot, 0xa000, ram_byte);
  {
    unsigned char install[] = {0xa2, 0,    0xbd, 0,    9,    0x9d, 0,
                               0,    0xe8, 0xd0, 0xf7, 0x4c, 0,    0};
    install[6] = install[12] = (unsigned char)driver.address;
    install[7] = install[13] = (unsigned char)(driver.address >> 8);
    for (i = 0; i < 14; ++i)
      store(&boot, 0x0200 + i, install[i]);
  }
  emit(&boot, 0x4c, 0x0200, 3);
  if (boot.failed || driver.failed || boot.size > 0x08ff - 0x080d) {
    fputs("error: standalone SID launcher exceeds its page\n", stderr);
    goto done;
  }
  image[0] = 1;
  image[1] = 8;
  {
    static const unsigned char basic_header[] = {11,  8,   10,  0, 158, '2',
                                                 '0', '6', '1', 0, 0,   0};
    memcpy(image + 2, basic_header, sizeof(basic_header));
  }
  memcpy(image + 2 + 0x080d - 0x0801, boot.bytes, (size_t)boot.size);
  memcpy(image + 2 + 0x0900 - 0x0801, driver.bytes, (size_t)driver.size);
  memcpy(image + 2 + stage - 0x0801, data + sid.offset, (size_t)sid.length);
  if (mapping) {
    file = fopen(mapping, "w");
    if (!file) {
      fprintf(stderr, "error: cannot create '%s'\n", mapping);
      goto done;
    }
    fprintf(file,
            "rsid_driver $%04X %d\nrsid_payload $%04X %d\nrsid_staging $%04X "
            "%d\nrsid_init $%04X\nrsid_song %d\nrsid_basic %d\n",
            driver.address, driver.size, sid.load, sid.length, stage,
            sid.length, sid.init, selected, !!(sid.flags & 2));
    if (fclose(file)) {
      file = NULL;
      goto done;
    }
    file = NULL;
  }
  if (!platform_write(output, image, (size_t)(end - 0x0801 + 2)))
    goto done;
  printf("Wrote %s\nStandalone RSID: song %d, load $%04X, launcher $%04X (%d "
         "bytes)\n",
         output, selected, sid.load, driver.address, driver.size);
  rc = 0;
  if (run) {
    if (!desktop_absolute(output, absolute)) {
      rc = 1;
      goto done;
    }
    if (!vice) {
      vice = getenv("PYC64_VICE");
      if (!vice || !*vice)
        vice = "x64sc";
    }
    rc = desktop_launch(vice, absolute, 0);
  }
done:
  if (file)
    fclose(file);
  free(data);
  free(image);
  return rc;
}

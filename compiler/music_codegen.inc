enum {
  M_ACTIVE = 0,
  M_SONG = 1,
  M_PLAY = 2,
  M_CALL = 4,
  M_SOURCE = 6,
  M_DEST = 8,
  M_SIZE = 10,
  M_TICKS = 12,
  M_ARG = 14,
  M_REQUEST = 16,
  M_HANDLE = 18,
  M_FLAGS = 20,
  M_TIMING = 21,
  M_CIA_OWNED = 22,
  M_OLD_CRA = 23,
  M_OLD_TA = 24,
  M_ICR = 26,
  M_NEXT_TIMING = 27,
  M_OLD_CRB = 28,
  M_OLD_TB = 29,
  M_RSID_IRQ = 32,
  M_RSID_RETURN = 34,
  M_ENTRY_A = 36,
  M_ENTRY_X = 37,
  M_ENTRY_Y = 38,
  M_OLD_VIC11 = 39,
  M_OLD_NMI = 40,
  M_SAVED_A = 42,
  M_SAVED_X = 43,
  M_SAVED_Y = 44,
  M_NATIVE_PORT = 45,
  M_STATE_SIZE = 46
};
static void music_flags_begin(Backend *g) {
  ins(g, 0x08, 0, 1);
  ins(g, 0x78, 0, 1);
  ins(g, 0x68, 0, 1);
  ref(g, 0x8d, g->music_work, M_FLAGS);
}
static void music_flags_end(Backend *g) {
  ref(g, 0xad, g->music_work, M_FLAGS);
  ins(g, 0x48, 0, 1);
  ins(g, 0x28, 0, 1);
}
static void music_word(Backend *g, int offset, int value) {
  ins(g, 0xa9, value & 255, 2);
  ref(g, 0x8d, g->music_work, offset);
  ins(g, 0xa9, value >> 8, 2);
  ref(g, 0x8d, g->music_work, offset + 1);
}
static void music_choose_timing(Backend *g, Asset *asset, SidInfo *info) {
  int ready, zero, done;
  if (info->rsid) {
    ins(g, 0xa9, 2, 2);
    ref(g, 0x8d, g->music_work, M_NEXT_TIMING);
    return;
  }
  if (!info->speed || info->speed == UINT32_C(0xffffffff)) {
    ins(g, 0xa9, info->speed ? 1 : 0, 2);
    ref(g, 0x8d, g->music_work, M_NEXT_TIMING);
    return;
  }
  ref(g, 0xad, g->music_work, M_REQUEST);
  ins(g, 0x38, 0, 1);
  ins(g, 0xe9, 1, 2);
  if (info->version == 1)
    ins(g, 0x29, 31, 2);
  else {
    ins(g, 0xc9, 32, 2);
    branch(g, 0x90, ready = label(g));
    ins(g, 0xa9, 31, 2);
    bind(g, ready);
  }
  ins(g, 0xaa, 0, 1);
  ins(g, 0x29, 7, 2);
  ins(g, 0xa8, 0, 1);
  if (!g->sprite_masks)
    g->sprite_masks = label(g);
  ref(g, 0xb9, g->sprite_masks, 0);
  ref(g, 0x8d, g->music_work, M_NEXT_TIMING);
  ins(g, 0x8a, 0, 1);
  ins(g, 0x4a, 0, 1);
  ins(g, 0x4a, 0, 1);
  ins(g, 0x4a, 0, 1);
  ins(g, 0x49, 3, 2);
  ins(g, 0xaa, 0, 1);
  ref(g, 0xbd, g->bytes_labels[asset->offset], 2 + 18);
  ref(g, 0x2d, g->music_work, M_NEXT_TIMING);
  branch(g, 0xf0, zero = label(g));
  ins(g, 0xa9, 1, 2);
  jump(g, done = label(g));
  bind(g, zero);
  ins(g, 0xa9, 0, 2);
  bind(g, done);
  ref(g, 0x8d, g->music_work, M_NEXT_TIMING);
}
static void music_call(Backend *g, int builtin, int *args) {
  int j;
  if (builtin == BI_MUSIC_ACTIVE) {
    ref(g, 0xad, g->music_work, M_ACTIVE);
    ins(g, 0x85, R, 2);
    ins(g, 0xa9, 0, 2);
    ins(g, 0x85, RH, 2);
    return;
  }
  if (builtin == BI_MUSIC_TICKS) {
    ins(g, 0x08, 0, 1);
    ins(g, 0x78, 0, 1);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->music_work, M_TICKS + j);
      ins(g, 0x85, R + j, 2);
    }
    ins(g, 0x28, 0, 1);
    return;
  }
  if (builtin == BI_MUSIC_START) {
    expr(g, args[0]);
    push(g);
    expr(g, args[1]);
    push(g);
    music_flags_begin(g);
    pop_lhs(g);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->lhs, j);
      ref(g, 0x8d, g->music_work, M_REQUEST + j);
    }
    pop_lhs(g);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->lhs, j);
      ref(g, 0x8d, g->music_work, M_HANDLE + j);
    }
    ref(g, 0x20, g->music_start, 0);
  } else {
    music_flags_begin(g);
    ref(g, 0x20, g->music_stop, 0);
  }
  music_flags_end(g);
}
static void music_transfer_zp(Backend *g, int ram, int first, int count,
                              int restore) {
  int columns = count / 16, j, loop = label(g);
  /* Sixteen lanes amortize loop overhead without a 5 KB fully unrolled copy. */
  ins(g, 0xa2, 0, 2);
  bind(g, loop);
  for (j = 0; j < columns; ++j) {
    if (restore) {
      ref(g, 0xbd, ram, j * 16);
      ins(g, 0x95, first + j * 16, 2);
    } else {
      ins(g, 0xb5, first + j * 16, 2);
      ref(g, 0x9d, ram, j * 16);
    }
  }
  ins(g, 0xe8, 0, 1);
  ins(g, 0xe0, 16, 2);
  branch(g, 0xd0, loop);
  for (j = columns * 16; j < count; ++j) {
    if (restore) {
      ref(g, 0xad, ram, j);
      ins(g, 0x85, first + j, 2);
    } else {
      ins(g, 0xa5, first + j, 2);
      ref(g, 0x8d, ram, j);
    }
  }
}
static void emit_music_cia_helpers(Backend *g) {
  int j, done, ntsc;
  bind(g, g->music_cia_save);
  for (j = 0; j < 2; ++j) {
    int control = j ? M_OLD_CRB : M_OLD_CRA;
    int latch = j ? M_OLD_TB : M_OLD_TA;
    ins(g, 0xad, 0xdc0e + j, 3);
    ref(g, 0x8d, g->music_work, control);
    ins(g, 0x29, 0xfe, 2);
    ins(g, 0x09, 0x10,
        2); /* Stop and load to read the otherwise hidden latch. */
    ins(g, 0x8d, 0xdc0e + j, 3);
    ins(g, 0xad, 0xdc04 + j * 2, 3);
    ref(g, 0x8d, g->music_work, latch);
    ins(g, 0xad, 0xdc05 + j * 2, 3);
    ref(g, 0x8d, g->music_work, latch + 1);
  }
  ins(g, 0x60, 0, 1);
  bind(g, g->music_cia_prepare);
  ref(g, 0x20, g->music_cia_save, 0);
  /* Timer B replaces the borrowed KERNAL timer at its original period. */
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->music_work, M_OLD_TA + j);
    ins(g, 0x8d, 0xdc06 + j, 3);
  }
  ins(g, 0xad, 0x02a6, 3);
  branch(g, 0xf0, ntsc = label(g));
  ins(g, 0xa9, 0x25, 2);
  ins(g, 0x8d, 0xdc04, 3);
  ins(g, 0xa9, 0x40, 2);
  ins(g, 0x8d, 0xdc05, 3);
  jump(g, done = label(g));
  bind(g, ntsc);
  ins(g, 0xa9, 0x95, 2);
  ins(g, 0x8d, 0xdc04, 3);
  ins(g, 0xa9, 0x42, 2);
  ins(g, 0x8d, 0xdc05, 3);
  bind(g, done);
  ref(g, 0xad, g->music_work, M_OLD_CRA);
  ins(g, 0x29, 0x80, 2);
  ins(g, 0x09, 0x11, 2);
  ins(g, 0x8d, 0xdc0e, 3);
  ins(g, 0xa9, 1, 2);
  ref(g, 0x8d, g->music_work, M_CIA_OWNED);
  ins(g, 0x60, 0, 1);
  bind(g, g->music_cia_restore);
  ref(g, 0xad, g->music_work, M_CIA_OWNED);
  branch(g, 0xf0, done = label(g));
  ins(g, 0xa9, 2, 2);
  ins(g, 0x8d, 0xdc0d, 3); /* Release only timer B's IRQ bit. */
  for (j = 0; j < 2; ++j) {
    int control = j ? M_OLD_CRB : M_OLD_CRA;
    int latch = j ? M_OLD_TB : M_OLD_TA;
    ref(g, 0xad, g->music_work, control);
    ins(g, 0x29, 0xfe, 2);
    ins(g, 0x8d, 0xdc0e + j, 3);
    ref(g, 0xad, g->music_work, latch);
    ins(g, 0x8d, 0xdc04 + j * 2, 3);
    ref(g, 0xad, g->music_work, latch + 1);
    ins(g, 0x8d, 0xdc05 + j * 2, 3);
    ref(g, 0xad, g->music_work, control);
    ins(g, 0x09, 0x10, 2);
    ins(g, 0x8d, 0xdc0e + j, 3);
  }
  ins(g, 0xad, 0xdc0d,
      3); /* Clear setup/stop flags; counters restart at the saved periods. */
  ins(g, 0xa9, 0x81, 2);
  ins(g, 0x8d, 0xdc0d, 3);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->music_work, M_CIA_OWNED);
  bind(g, done);
  ins(g, 0x60, 0, 1);
}
static int music_state_size(Backend *g) {
  return g->music_rsid_invoke ? M_STATE_SIZE : 32;
}
static void emit_music_rsid_helpers(Backend *g) {
  int j, done;
  if (!g->music_rsid_invoke)
    return;
  bind(g, g->music_rsid_prepare);
  ref(g, 0x20, g->music_cia_prepare, 0);
  for (j = 0; j < 2; ++j) {
    ins(g, 0xad, 0x0318 + j, 3);
    ref(g, 0x8d, g->music_work, M_OLD_NMI + j);
  }
  ins(g, 0xad, 0xd011, 3);
  ins(g, 0x29, 127, 2);
  ref(g, 0x8d, g->music_work, M_OLD_VIC11);
  ins(g, 0x09, 128, 2);
  ins(g, 0x8d, 0xd011, 3);
  ins(g, 0xa9, 0x37, 2);
  ins(g, 0x8d, 0xd012, 3);
  ins(g, 0xa9, 0, 2);
  ins(g, 0x8d, 0xd01a, 3);
  ins(g, 0x8d, 0xdc0f, 3);
  ins(g, 0xa9, 255, 2);
  ins(g, 0x8d, 0xdc06, 3);
  ins(g, 0x8d, 0xdc07, 3);
  ins(g, 0xa9, 0x7f, 2);
  ins(g, 0x8d, 0xdc0d, 3);
  ins(g, 0xad, 0xdc0d, 3);
  ins(g, 0xa9, 0x81, 2);
  ins(g, 0x8d, 0xdc0d, 3);
  set_pointer(g, g->music_rsid_resume, 0);
  for (j = 0; j < 2; ++j) {
    ins(g, 0xa5, P + j, 2);
    ref(g, 0x8d, g->music_work, M_RSID_RETURN + j);
  }
  ins(g, 0x60, 0, 1);
  bind(g, g->music_rsid_restore);
  ref(g, 0xad, g->music_work, M_TIMING);
  ins(g, 0xc9, 2, 2);
  branch(g, 0xd0, done = label(g));
  ref(g, 0xad, g->music_work, M_CIA_OWNED);
  branch(g, 0xf0, done);
  ins(g, 0xa9, 0x7f, 2);
  ins(g, 0x8d, 0xdc0d, 3);
  ins(g, 0xa9, 0, 2);
  ins(g, 0x8d, 0xd01a, 3);
  ins(g, 0xa9, 15, 2);
  ins(g, 0x8d, 0xd019, 3);
  ref(g, 0xad, g->music_work, M_OLD_VIC11);
  ins(g, 0x09, 128, 2);
  ins(g, 0x8d, 0xd011, 3);
  ins(g, 0xa9, 0x37, 2);
  ins(g, 0x8d, 0xd012, 3);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->music_work, M_OLD_NMI + j);
    ins(g, 0x8d, 0x0318 + j, 3);
    ref(g, 0xad, g->irq_work, j);
    ins(g, 0x8d, 0x0314 + j, 3);
  }
  bind(g, done);
  ins(g, 0x60, 0, 1);

  bind(g, g->music_rsid_invoke);
  music_transfer_zp(g, g->music_foreground, 0, 256, 0);
  music_transfer_zp(g, g->music_zp, 2, 254, 1);
  ins(g, 0xa9, 0x2f, 2);
  ins(g, 0x85, 0, 2);
  ins(g, 0xa9, 0x37, 2);
  ins(g, 0x85, 1, 2);
  /* A synthetic hardware/KERNAL frame lets an unmodified IRQ routine use
     RTI (directly or through the ROM epilogue), then resume the wrapper. */
  ref(g, 0xad, g->music_work, M_RSID_RETURN + 1);
  ins(g, 0x48, 0, 1);
  ref(g, 0xad, g->music_work, M_RSID_RETURN);
  ins(g, 0x48, 0, 1);
  ins(g, 0x08, 0, 1);
  ins(g, 0x68, 0, 1);
  ins(g, 0x29, 0xef, 2);
  ins(g, 0x48, 0, 1);
  for (j = 0; j < 3; ++j) {
    ref(g, 0xad, g->music_work, M_SAVED_A + j);
    ins(g, 0x48, 0, 1);
  }
  ref(g, 0xac, g->music_work, M_ENTRY_Y);
  ins(g, 0xba, 0, 1);
  ref(g, 0xad, g->music_work, M_ENTRY_A);
  ref(g, 0x6c, g->music_work, M_RSID_IRQ);
  bind(g, g->music_rsid_resume);
  ins(g, 0x78, 0, 1);
  ins(g, 0xd8, 0, 1);
  music_transfer_zp(g, g->music_zp, 2, 254, 0);
  music_transfer_zp(g, g->music_foreground, 0, 256, 1);
  ins(g, 0x60, 0, 1);
}
static void emit_music_helpers(Backend *g) {
  Compiler *c = g->c;
  int i, j, loop, done, reject, next, chosen, copy, skip, call;
  if (!g->music_work)
    return;
  emit_music_cia_helpers(g);
  emit_music_rsid_helpers(g);
  bind(g, g->music_empty);
  ins(g, 0x60, 0, 1);
  bind(g, g->music_silence);
  if (g->music_rsid_restore)
    ref(g, 0x20, g->music_rsid_restore, 0);
  ref(g, 0x20, g->music_cia_restore, 0);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->music_work, M_ACTIVE);
  ins(g, 0xa2, 24, 2);
  bind(g, loop = label(g));
  ins(g, 0x9d, 0xd400, 3);
  ins(g, 0xca, 0, 1);
  branch(g, 0x10, loop);
  ins(g, 0x60, 0, 1);

  bind(g, g->music_invoke);
  /* Save the entire caller page, including the 6510 port, then install the
     tune's private $02..$FF state. No compiler scratch is visible to it. */
  music_transfer_zp(g, g->music_foreground, 0, 256, 0);
  music_transfer_zp(g, g->music_zp, 2, 254, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->music_foreground, j);
    ins(g, 0x09, 7, 2);
    ins(g, 0x85, j, 2);
  }
  if (g->music_rsid_invoke) {
    ref(g, 0xad, g->music_work, M_TIMING);
    ins(g, 0xc9, 2, 2);
    branch(g, 0xd0, done = label(g));
    ins(g, 0xa9, 0x2f, 2);
    ins(g, 0x85, 0, 2);
    ins(g, 0xa9, 0x37, 2);
    ins(g, 0x85, 1, 2);
    bind(g, done);
  }
  ins(g, 0xd8, 0, 1);
  ref(g, 0xad, g->music_work, M_ARG);
  ins(g, 0xa2, 0, 2);
  ins(g, 0xa0, 0, 2);
  ref(g, 0x20, call = label(g), 0);
  ins(g, 0x78, 0, 1);
  ins(g, 0xd8, 0, 1);
  if (g->music_rsid_invoke) {
    ins(g, 0xa5, 1, 2);
    ref(g, 0x8d, g->music_work, M_NATIVE_PORT);
  }
  music_transfer_zp(g, g->music_zp, 2, 254, 0);
  music_transfer_zp(g, g->music_foreground, 0, 256, 1);
  ins(g, 0x60, 0, 1);
  bind(g, call);
  ref(g, 0x6c, g->music_work, M_CALL);

  bind(g, g->music_tick);
  ref(g, 0xad, g->music_work, M_ACTIVE);
  branch(g, 0xf0, done = label(g));
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->music_work, M_PLAY + j);
    ref(g, 0x8d, g->music_work, M_CALL + j);
  }
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->music_work, M_ARG);
  ref(g, 0x20, g->music_invoke, 0);
  ref(g, 0xee, g->music_work, M_TICKS);
  branch(g, 0xd0, done);
  ref(g, 0xee, g->music_work, M_TICKS + 1);
  bind(g, done);
  ins(g, 0x60, 0, 1);

  bind(g, g->music_start);
  reject = label(g);
  for (i = 0; i < c->nassets; ++i) {
    Asset *asset = &c->assets[i];
    SidInfo info;
    char error[256];
    if (asset->mode != AS_MUSIC || !g->bytes_labels[asset->offset])
      continue;
    sid_info(c->bytes + asset->offset, asset->count, &info, error,
             sizeof(error));
    next = label(g);
    set_pointer(g, g->bytes_labels[asset->offset], 2);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->music_work, M_HANDLE + j);
      ins(g, 0xc5, P + j, 2);
      branch(g, 0xd0, next);
    }
    /* A request of zero selects the header's default; otherwise songs are
       numbered 1..256 publicly and passed to init as the 0-based byte. */
    ref(g, 0xad, g->music_work, M_REQUEST);
    ref(g, 0x0d, g->music_work, M_REQUEST + 1);
    branch(g, 0xd0, chosen = label(g));
    music_word(g, M_REQUEST, info.start);
    bind(g, chosen);
    ref(g, 0xad, g->music_work, M_REQUEST + 1);
    ins(g, 0xc9, info.songs >> 8, 2);
    branch(g, 0x90, chosen = label(g));
    branch(g, 0xd0, reject);
    ref(g, 0xad, g->music_work, M_REQUEST);
    ins(g, 0xc9, (info.songs & 255) + 1, 2);
    if ((info.songs & 255) == 255) {
      /* All low bytes fit when the high byte matched zero. */
    } else
      branch(g, 0xb0, reject);
    bind(g, chosen);
    music_choose_timing(g, asset, &info);
    ref(g, 0xad, g->music_work, M_NEXT_TIMING);
    branch(g, 0xf0, chosen = label(g));
    ref(g, 0xad, g->music_work, M_CIA_OWNED);
    branch(g, 0xd0, chosen); /* Replacing our own CIA player is safe. */
    /* CIA status is read-to-clear and its mask is unreadable. Borrow only
       standard KERNAL timer A, with an idle timer B and the known old vector.
     */
    ref(g, 0xad, g->irq_work, 2);
    branch(g, 0xf0, skip = label(g));
    ref(g, 0xad, g->irq_work, 0);
    ins(g, 0xc9, 0x31, 2);
    branch(g, 0xd0, reject);
    ref(g, 0xad, g->irq_work, 1);
    ins(g, 0xc9, 0xea, 2);
    branch(g, 0xd0, reject);
    jump(g, done = label(g));
    bind(g, skip);
    ins(g, 0xad, 0x0314, 3);
    ins(g, 0xc9, 0x31, 2);
    branch(g, 0xd0, reject);
    ins(g, 0xad, 0x0315, 3);
    ins(g, 0xc9, 0xea, 2);
    branch(g, 0xd0, reject);
    bind(g, done);
    ins(g, 0xad, 0xdc0e, 3);
    ins(g, 0x29, 0x7f, 2);
    ins(g, 0xc9, 1, 2);
    branch(g, 0xd0, reject);
    ins(g, 0xad, 0xdc0f, 3);
    ins(g, 0x29, 0x77, 2); /* KERNAL leaves stopped B in one-shot mode. */
    branch(g, 0xd0, reject);
    bind(g, chosen);
    if (info.rsid) {
      ref(g, 0xad, g->music_work, M_FLAGS);
      ins(g, 0x29, 4, 2);
      branch(g, 0xd0, reject); /* Returning RSID init is allowed to use CLI. */
      ref(g, 0xad, g->irq_work, 2);
      branch(g, 0xf0, chosen = label(g));
      set_pointer(g, g->music_empty, 0);
      for (j = 0; j < 2; ++j) {
        ref(g, 0xad, g->irq_work, 4 + j);
        ins(g, 0xc5, P + j, 2);
        branch(g, 0xd0, reject);
      }
      bind(g, chosen);
    }
    ref(g, 0xad, g->irq_work, 2);
    branch(g, 0xd0, chosen = label(g));
    ins(g, 0xad, 0xd01a, 3);
    ins(g, 0x29, info.rsid ? 15 : 1, 2);
    branch(g, 0xd0, reject);
    bind(g, chosen);
    if (g->music_rsid_restore) {
      ref(g, 0xad, g->music_work, M_TIMING);
      ins(g, 0x48, 0, 1);
      ref(g, 0x20, g->music_silence, 0);
      ins(g, 0x68, 0, 1);
      ins(g, 0xc9, 2, 2);
      branch(g, 0xd0, chosen = label(g));
      ref(g, 0x20, g->irq_stop, 0);
      bind(g, chosen);
    } else
      ref(g, 0x20, g->music_silence, 0);
    ref(g, 0xad, g->music_work, M_NEXT_TIMING);
    ref(g, 0x8d, g->music_work, M_TIMING);
    if (info.rsid) {
      ref(g, 0x20, g->irq_stop, 0);
      for (j = 0; j < 2; ++j) {
        ins(g, 0xad, 0x0314 + j, 3);
        ref(g, 0x8d, g->irq_work, j);
      }
      ref(g, 0x20, g->music_rsid_prepare, 0);
      set_pointer(g, g->music_empty, 0);
      for (j = 0; j < 2; ++j) {
        ins(g, 0xa5, P + j, 2);
        ref(g, 0x8d, g->irq_work, 4 + j);
      }
      ins(g, 0xa9, 1, 2);
      ref(g, 0x8d, g->irq_work, 2);
      ref(g, 0x8d, g->music_work, M_ACTIVE);
    } else {
      branch(g, 0xf0, chosen = label(g));
      ref(g, 0x20, g->music_cia_prepare, 0);
      bind(g, chosen);
    }
    ref(g, 0xad, g->music_work, M_REQUEST);
    ins(g, 0x38, 0, 1);
    ins(g, 0xe9, 1, 2);
    ref(g, 0x8d, g->music_work, M_SONG);
    ref(g, 0x8d, g->music_work, M_ARG);
    music_word(g, M_CALL, info.init);
    music_word(g, M_PLAY, info.play);
    music_word(g, M_DEST, info.load);
    music_word(g, M_SIZE, info.length);
    music_word(g, M_TICKS, 0);
    set_pointer(g, g->bytes_labels[asset->offset], 2 + info.offset);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->music_work, M_DEST + j);
      ins(g, 0x85, R + j, 2);
    }
    ins(g, 0xa0, 0, 2);
    bind(g, copy = label(g));
    ins(g, 0xb1, P, 2);
    ins(g, 0x91, R, 2);
    ins(g, 0xe6, P, 2);
    branch(g, 0xd0, skip = label(g));
    ins(g, 0xe6, PH, 2);
    bind(g, skip);
    ins(g, 0xe6, R, 2);
    branch(g, 0xd0, skip = label(g));
    ins(g, 0xe6, RH, 2);
    bind(g, skip);
    ref(g, 0xad, g->music_work, M_SIZE);
    branch(g, 0xd0, skip = label(g));
    ref(g, 0xce, g->music_work, M_SIZE + 1);
    bind(g, skip);
    ref(g, 0xce, g->music_work, M_SIZE);
    ref(g, 0xad, g->music_work, M_SIZE);
    ref(g, 0x0d, g->music_work, M_SIZE + 1);
    branch(g, 0xd0, copy);
    if (info.rsid)
      music_transfer_zp(g, g->music_zp, 2, 254, 0);
    else {
      ins(g, 0xa9, 0, 2);
      ins(g, 0xa2, 0, 2);
      bind(g, loop = label(g));
      ref(g, 0x9d, g->music_zp, 0);
      ins(g, 0xe8, 0, 1);
      ins(g, 0xe0, 254, 2);
      branch(g, 0xd0, loop);
    }
    ref(g, 0x20, g->music_invoke, 0);
    if (info.rsid) {
      int bad_init = label(g), good_init = label(g);
      ref(g, 0xad, g->music_work, M_NATIVE_PORT);
      ins(g, 0x29, 7, 2);
      ins(g, 0xc9, 7, 2);
      branch(g, 0xd0, bad_init);
      /* Init must leave a KERNAL-mapped IRQ routine inside the reserved image.
         NMI ownership and ROM banking need a separate execution profile. */
      for (j = 0; j < 2; ++j) {
        ins(g, 0xad, 0x0318 + j, 3);
        ref(g, 0xcd, g->music_work, M_OLD_NMI + j);
        branch(g, 0xd0, bad_init);
        ins(g, 0xad, 0x0314 + j, 3);
        ref(g, 0x8d, g->music_work, M_RSID_IRQ + j);
      }
      /* Compare the captured vector as an unsigned word. */
      for (j = 0; j < 2; ++j) {
        int boundary = j ? info.load + info.length : info.load;
        ref(g, 0xad, g->music_work, M_RSID_IRQ + 1);
        ins(g, 0xc9, boundary >> 8, 2);
        chosen = label(g);
        if (j) {
          branch(g, 0x90, chosen);
          branch(g, 0xd0, bad_init);
        } else {
          branch(g, 0x90, bad_init);
          branch(g, 0xd0, chosen);
        }
        ref(g, 0xad, g->music_work, M_RSID_IRQ);
        ins(g, 0xc9, boundary & 255, 2);
        branch(g, j ? 0xb0 : 0x90, bad_init);
        bind(g, chosen);
      }
      set_pointer(g, g->irq_hook, 0);
      for (j = 0; j < 2; ++j) {
        ins(g, 0xa5, P + j, 2);
        ins(g, 0x8d, 0x0314 + j, 3);
      }
      jump(g, good_init);
      bind(g, bad_init);
      ref(g, 0x20, g->irq_stop, 0);
      jump(g, reject);
      bind(g, good_init);
      constant(g, 1);
      ins(g, 0x60, 0, 1);
      bind(g, next);
      continue;
    }
    ref(g, 0xad, g->music_work, M_TIMING);
    branch(g, 0xf0, chosen = label(g));
    /* Init may choose a custom timer-A latch. Load that period now. */
    ins(g, 0xad, 0xdc0e, 3);
    ins(g, 0x29, 0x80, 2);
    ins(g, 0x09, 0x11, 2);
    ins(g, 0x8d, 0xdc0e, 3);
    ins(g, 0xad, 0xdc0f, 3);
    ins(g, 0x29, 0x80, 2);
    ins(g, 0x09, 0x11, 2);
    ins(g, 0x8d, 0xdc0f, 3);
    ins(g, 0xad, 0xdc0d, 3);
    ins(g, 0xa9, 0x83, 2);
    ins(g, 0x8d, 0xdc0d, 3);
    bind(g, chosen);
    ref(g, 0xad, g->irq_work, 2);
    branch(g, 0xf0, skip = label(g));
    set_pointer(g, g->music_empty, 0);
    chosen = label(g);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->irq_work, 4 + j);
      ins(g, 0xc5, P + j, 2);
      branch(g, 0xd0, chosen);
    }
    jump(g, done = label(g));
    bind(g, skip);
    set_pointer(g, g->music_empty, 0);
    for (j = 0; j < 2; ++j) {
      ins(g, 0xa5, P + j, 2);
      ref(g, 0x8d, g->irq_work, 4 + j);
    }
    ins(g, 0xa9, 250, 2);
    ref(g, 0x8d, g->irq_work, 6);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->irq_work, 7);
    bind(g, done);
    ref(g, 0x20, g->irq_install, 0);
    ref(g, 0xad, g->music_work, M_TIMING);
    branch(g, 0xf0, chosen);
    ins(g, 0xad, 0xd01a, 3);
    ins(g, 0x29, 0xfe, 2);
    ins(g, 0x8d, 0xd01a, 3);
    bind(g, chosen);
    ins(g, 0xa9, 1, 2);
    ref(g, 0x8d, g->music_work, M_ACTIVE);
    constant(g, 1);
    ins(g, 0x60, 0, 1);
    bind(g, next);
  }
  bind(g, reject);
  constant(g, 0);
  ins(g, 0x60, 0, 1);

  bind(g, g->music_stop);
  ref(g, 0xad, g->music_work, M_ACTIVE);
  branch(g, 0xd0, chosen = label(g));
  constant(g, 0);
  ins(g, 0x60, 0, 1);
  bind(g, chosen);
  ref(g, 0x20, g->music_silence, 0);
  set_pointer(g, g->music_empty, 0);
  done = label(g);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->irq_work, 4 + j);
    ins(g, 0xc5, P + j, 2);
    branch(g, 0xd0, done);
  }
  ref(g, 0x20, g->irq_stop, 0);
  bind(g, done);
  constant(g, 1);
  ins(g, 0x60, 0, 1);
}

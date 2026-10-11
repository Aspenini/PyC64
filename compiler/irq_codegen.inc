/* KERNAL's IRQ entry has already saved A/X/Y and the hardware return frame.
   Preserve compiler scratch and every frame reachable from the callback. */
static void irq_flags_begin(Backend *g) {
  ins(g, 0x08, 0, 1);
  ins(g, 0x78, 0, 1);
  ins(g, 0x68, 0, 1);
  ref(g, 0x8d, g->irq_work, 8);
}
static void irq_flags_end(Backend *g) {
  ref(g, 0xad, g->irq_work, 8);
  ins(g, 0x48, 0, 1);
  ins(g, 0x28, 0, 1);
}
static void irq_call(Backend *g, int builtin, Node *n, int *args) {
  int j, refused = 0, completed;
  if (builtin == BI_IRQ_DISABLE) {
    ins(g, 0x08, 0, 1);
    ins(g, 0x78, 0, 1);
    ins(g, 0x68, 0, 1);
    ins(g, 0x29, 4, 2);
    ins(g, 0x4a, 0, 1);
    ins(g, 0x4a, 0, 1);
    ins(g, 0x85, R, 2);
    ins(g, 0xa9, 0, 2);
    ins(g, 0x85, RH, 2);
    return;
  }
  if (builtin == BI_IRQ_RESTORE) {
    int masked = label(g), done = label(g);
    expr(g, args[0]);
    ins(g, 0xa5, R, 2);
    branch(g, 0xd0, masked);
    if (g->irq_work) {
      ref(g, 0xad, g->irq_work, 3);
      branch(g, 0xd0, g->trap); /* Never enable nested callbacks. */
    }
    ins(g, 0x58, 0, 1);
    jump(g, done);
    bind(g, masked);
    ins(g, 0x78, 0, 1);
    bind(g, done);
    return;
  }
  if (builtin == BI_IRQ_ACTIVE) {
    ref(g, 0xad, g->irq_work, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa9, 0, 2);
    ins(g, 0x85, RH, 2);
    return;
  }
  if (builtin == BI_IRQ_OFF) {
    irq_flags_begin(g);
    ref(g, 0x20, g->irq_stop, 0);
    irq_flags_end(g);
    return;
  }
  expr(g, args[0]);
  if (g->music_rsid_invoke) {
    int available = label(g);
    refused = label(g);
    ref(g, 0xad, g->music_work, M_ACTIVE);
    branch(g, 0xf0, available);
    ref(g, 0xad, g->music_work, M_TIMING);
    ins(g, 0xc9, 2, 2);
    branch(g, 0xf0, refused);
    bind(g, available);
  }
  ins(g, 0xa5, RH, 2);
  ins(g, 0xc9, 2, 2);
  branch(g, 0xb0, g->trap);
  push(g);
  irq_flags_begin(g);
  pop_lhs(g);
  ref(g, 0xad, g->lhs, 0);
  ref(g, 0x8d, g->irq_work, 6);
  ref(g, 0xad, g->lhs, 1);
  for (j = 0; j < 7; ++j)
    ins(g, 0x0a, 0, 1);
  ref(g, 0x8d, g->irq_work, 7);
  if (builtin == BI_IRQ_INSTALL) {
    set_pointer(g, g->c->functions[(int)n->value - 1].label, 0);
    for (j = 0; j < 2; ++j) {
      ins(g, 0xa5, P + j, 2);
      ref(g, 0x8d, g->irq_work, 4 + j);
    }
    ref(g, 0x20, g->irq_install, 0);
  } else
    ref(g, 0x20, g->irq_line, 0);
  irq_flags_end(g);
  if (refused) {
    jump(g, completed = label(g));
    bind(g, refused);
    constant(g, 0);
    bind(g, completed);
  }
}
static void irq_save_slot(Backend *g, int source, int size) {
  int index = g->irq_count++;
  if (!source || size <= 0) {
    --g->irq_count;
    return;
  }
  if (index >= MAX_SYMBOLS + MAX_NODES + 32)
    fail(g->c, 0, "IRQ backup descriptor arena exhausted");
  g->irq_labels[index] = source;
  g->irq_sizes[index] = size;
  g->irq_offsets[index] = g->irq_size;
  g->irq_size += size;
}
static void irq_copy_slots(Backend *g, int restore) {
  int slot, offset, j;
  for (slot = 0; slot < g->irq_count; ++slot) {
    int size = g->irq_sizes[slot], original = g->irq_labels[slot];
    for (offset = 0; offset < size; offset += 256) {
      int chunk = size - offset, loop;
      int source = restore ? g->irq_backup : original;
      int target = restore ? original : g->irq_backup;
      int from = offset + (restore ? g->irq_offsets[slot] : 0);
      int to = offset + (restore ? 0 : g->irq_offsets[slot]);
      if (chunk > 256)
        chunk = 256;
      if (chunk <= 8) {
        for (j = 0; j < chunk; ++j) {
          ref(g, 0xad, source, from + j);
          ref(g, 0x8d, target, to + j);
        }
      } else {
        ins(g, 0xa2, 0, 2);
        bind(g, loop = label(g));
        ref(g, 0xbd, source, from);
        ref(g, 0x9d, target, to);
        ins(g, 0xe8, 0, 1);
        if (chunk != 256)
          ins(g, 0xe0, chunk, 2);
        branch(g, 0xd0, loop);
      }
    }
  }
}
static void emit_irq_helpers(Backend *g) {
  Compiler *c = g->c;
  int i, j, inactive, owned, chain, rsid = 0, finish;
  if (!g->irq_work)
    return;
  g->irq_size = 4;
  irq_save_slot(g, g->lhs, 2);
  irq_save_slot(g, g->math, 2);
  irq_save_slot(g, g->arguments, g->argument_count * 2);
  irq_save_slot(g, g->division_work, 4);
  irq_save_slot(g, g->power_work, 6);
  irq_save_slot(g, g->enum_work, 2);
  irq_save_slot(g, g->string_work, 6);
  irq_save_slot(g, g->bytes_work,
                g->bytes_search                    ? 32
                : g->bytes_slice                   ? 26
                : g->bytes_join || g->bytes_repeat ? 20
                                                   : 12);
  irq_save_slot(g, g->reu_flags, 1);
  irq_save_slot(g, g->float_result, 5);
  irq_save_slot(g, g->float_left, 5);
  for (i = 0; i < c->nsymbols; ++i) {
    Symbol *s = &c->symbols[i];
    if (s->scope && c->functions[s->scope - 1].irq && live_symbol(c, s))
      irq_save_slot(g, s->label, symbol_size(s));
  }
  for (i = 1; i <= c->nnodes; ++i)
    if (g->irq_temps[i]) {
      Node *n = &c->nodes[i];
      if (g->for_labels[i])
        irq_save_slot(g, g->for_labels[i],
                      n->kind == N_EACH
                          ? (c->nodes[n->b].type == TY_STR ? 2 : 4)
                      : n->op ? 6
                              : 2);
      if (g->list_labels[i])
        irq_save_slot(g, g->list_labels[i], n->d * type_size(n->c));
    }
  bind(g, g->irq_install);
  owned = label(g);
  ref(g, 0xad, g->irq_work, 2);
  branch(g, 0xd0, owned);
  ins(g, 0xad, 0xd01a, 3);
  ins(g, 0x29, 1, 2);
  branch(g, 0xf0, inactive = label(g));
  constant(g, 0); /* Another owner already uses raster interrupts. */
  ins(g, 0x60, 0, 1);
  bind(g, inactive);
  for (j = 0; j < 2; ++j) {
    ins(g, 0xad, 0x0314 + j, 3);
    ref(g, 0x8d, g->irq_work, j);
  }
  set_pointer(g, g->irq_hook, 0);
  for (j = 0; j < 2; ++j) {
    ins(g, 0xa5, P + j, 2);
    ins(g, 0x8d, 0x0314 + j, 3);
  }
  ins(g, 0xa9, 1, 2);
  ref(g, 0x8d, g->irq_work, 2);
  bind(g, owned);
  ref(g, 0x20, g->irq_line, 0);
  ins(g, 0xa9, 1, 2);
  ins(g, 0x8d, 0xd019, 3);
  ins(g, 0xad, 0xd01a, 3);
  ins(g, 0x09, 1, 2);
  ins(g, 0x8d, 0xd01a, 3);
  constant(g, 1);
  ins(g, 0x60, 0, 1);
  bind(g, g->irq_line);
  ref(g, 0xad, g->irq_work, 2);
  branch(g, 0xd0, owned = label(g));
  constant(g, 0);
  ins(g, 0x60, 0, 1);
  bind(g, owned);
  ref(g, 0xad, g->irq_work, 6);
  ins(g, 0x8d, 0xd012, 3);
  ins(g, 0xad, 0xd011, 3);
  ins(g, 0x29, 127, 2);
  ref(g, 0x0d, g->irq_work, 7);
  ins(g, 0x8d, 0xd011, 3);
  constant(g, 1);
  ins(g, 0x60, 0, 1);
  bind(g, g->irq_stop);
  ref(g, 0xad, g->irq_work, 2);
  branch(g, 0xd0, owned = label(g));
  constant(g, 0);
  ins(g, 0x60, 0, 1);
  bind(g, owned);
  if (g->music_work) {
    int quiet = label(g);
    ref(g, 0xad, g->music_work, 0);
    branch(g, 0xf0, quiet);
    ref(g, 0x20, g->music_silence, 0);
    bind(g, quiet);
  }
  ins(g, 0xad, 0xd01a, 3);
  ins(g, 0x29, 254, 2);
  ins(g, 0x8d, 0xd01a, 3);
  ins(g, 0xa9, 1, 2);
  ins(g, 0x8d, 0xd019, 3);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->irq_work, j);
    ins(g, 0x8d, 0x0314 + j, 3);
  }
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->irq_work, 2);
  constant(g, 1);
  ins(g, 0x60, 0, 1);
  bind(g, g->irq_hook);
  chain = label(g);
  finish = label(g);
  if (g->music_work) {
    int no_cia = label(g), raster = label(g), service = label(g);
    if (g->music_rsid_invoke) {
      ref(g, 0x8d, g->music_work, M_ENTRY_A);
      ref(g, 0x8e, g->music_work, M_ENTRY_X);
      ref(g, 0x8c, g->music_work, M_ENTRY_Y);
      ins(g, 0xba, 0, 1);
      for (j = 0; j < 3; ++j) {
        ins(g, 0xbd, 0x0101 + j, 3);
        ref(g, 0x8d, g->music_work, M_SAVED_Y - j);
      }
      ref(g, 0xad, g->music_work, M_ACTIVE);
      branch(g, 0xf0, no_cia);
      ref(g, 0xad, g->music_work, M_TIMING);
      ins(g, 0xc9, 2, 2);
      branch(g, 0xf0, rsid = label(g));
      bind(g, no_cia);
      no_cia = label(g);
    }
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->music_work, M_ICR);
    ref(g, 0x8d, g->irq_work, 9);
    ref(g, 0xad, g->music_work, M_CIA_OWNED);
    branch(g, 0xf0, no_cia);
    ins(g, 0xad, 0xdc0d, 3); /* Read-to-clear exactly once for this entry. */
    ref(g, 0x8d, g->music_work, M_ICR);
    bind(g, no_cia);
    ins(g, 0xad, 0xd019, 3);
    ins(g, 0x2d, 0xd01a, 3);
    ins(g, 0x29, 1, 2);
    branch(g, 0xd0, raster);
    ref(g, 0xad, g->music_work, M_ICR);
    ins(g, 0x29, 0x81, 2);
    ins(g, 0xc9, 0x81, 2);
    branch(g, 0xd0, chain);
    jump(g, service);
    bind(g, raster);
    ins(g, 0xa9, 1, 2);
    ins(g, 0x8d, 0xd019, 3);
    ref(g, 0x8d, g->irq_work, 9);
    bind(g, service);
    ins(g, 0xa9, 1, 2);
  } else {
    ins(g, 0xad, 0xd019, 3);
    ins(g, 0x29, 1, 2);
    branch(g, 0xf0, chain);
    ins(g, 0xad, 0xd01a, 3);
    ins(g, 0x29, 1, 2);
    branch(g, 0xf0, chain);
    ins(g, 0xa9, 1, 2);
    ins(g, 0x8d, 0xd019, 3);
  }
  ref(g, 0x8d, g->irq_work, 3);
  ins(g, 0xd8, 0, 1);
  for (j = 0; j < 4; ++j) {
    ins(g, 0xa5, P + j, 2);
    ref(g, 0x8d, g->irq_backup, j);
  }
  irq_copy_slots(g, 0);
  if (g->music_work) {
    int vbi = label(g), tick = label(g), no_tick = label(g);
    ref(g, 0xad, g->music_work, M_TIMING);
    branch(g, 0xf0, vbi);
    ref(g, 0xad, g->music_work, M_ICR);
    ins(g, 0x29, 0x81, 2);
    ins(g, 0xc9, 0x81, 2);
    branch(g, 0xd0, no_tick);
    jump(g, tick);
    bind(g, vbi);
    ref(g, 0xad, g->irq_work, 9);
    branch(g, 0xf0, no_tick);
    bind(g, tick);
    ref(g, 0x20, g->music_tick, 0);
    bind(g, no_tick);
    ref(g, 0xad, g->irq_work, 9);
    branch(g, 0xf0, no_tick = label(g));
    ref(g, 0x20, g->irq_dispatch, 0);
    bind(g, no_tick);
  } else
    ref(g, 0x20, g->irq_dispatch, 0);
  irq_copy_slots(g, 1);
  for (j = 0; j < 4; ++j) {
    ref(g, 0xad, g->irq_backup, j);
    ins(g, 0x85, P + j, 2);
  }
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->irq_work, 3);
  if (g->music_work) {
    ref(g, 0xad, g->music_work, M_ICR);
    ins(g, 0x29, 2, 2);
    branch(g, 0xd0, chain); /* Timer B keeps the original KERNAL cadence. */
  }
  bind(g, finish);
  ins(g, 0x68, 0, 1);
  ins(g, 0xa8, 0, 1);
  ins(g, 0x68, 0, 1);
  ins(g, 0xaa, 0, 1);
  ins(g, 0x68, 0, 1);
  ins(g, 0x40, 0, 1);
  if (rsid) {
    bind(g, rsid);
    ins(g, 0xa9, 1, 2);
    ref(g, 0x8d, g->irq_work, 3);
    ins(g, 0xd8, 0, 1);
    for (j = 0; j < 4; ++j) {
      ins(g, 0xa5, P + j, 2);
      ref(g, 0x8d, g->irq_backup, j);
    }
    irq_copy_slots(g, 0);
    ref(g, 0x20, g->music_rsid_invoke, 0);
    irq_copy_slots(g, 1);
    for (j = 0; j < 4; ++j) {
      ref(g, 0xad, g->irq_backup, j);
      ins(g, 0x85, P + j, 2);
    }
    ref(g, 0xee, g->music_work, M_TICKS);
    branch(g, 0xd0, owned = label(g));
    ref(g, 0xee, g->music_work, M_TICKS + 1);
    bind(g, owned);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->irq_work, 3);
    jump(g, finish);
  }
  bind(g, chain);
  ref(g, 0x6c, g->irq_work, 0);
  bind(g, g->irq_dispatch);
  ref(g, 0x6c, g->irq_work, 4);
  if (c->map)
    fprintf(c->map, "irq_handler $%04X backup=%d\n", g->labels[g->irq_hook],
            g->irq_size);
}

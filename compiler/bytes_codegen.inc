/* Immutable binary values use the same two-byte length header as strings.
   The shared work area is used only after operands have been evaluated. */
static void bytes_pointer(Backend *g, int slot) {
  int j;
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->bytes_work, slot + j);
    ins(g, 0x85, P + j, 2);
  }
}
static void bytes_store_word(Backend *g, int slot) {
  int j;
  for (j = 0; j < 2; ++j) {
    ins(g, 0xa5, R + j, 2);
    ref(g, 0x8d, g->bytes_work, slot + j);
  }
}
static void bytes_length(Backend *g, int pointer, int count) {
  bytes_pointer(g, pointer);
  length_call(g);
  bytes_store_word(g, count);
}
static void bytes_advance(Backend *g, int slot, int amount) {
  ins(g, 0x18, 0, 1);
  ref(g, 0xad, g->bytes_work, slot);
  ins(g, 0x69, amount, 2);
  ref(g, 0x8d, g->bytes_work, slot);
  ref(g, 0xad, g->bytes_work, slot + 1);
  ins(g, 0x69, 0, 2);
  ref(g, 0x8d, g->bytes_work, slot + 1);
}
static void bytes_decrement(Backend *g, int slot) {
  ins(g, 0x38, 0, 1);
  ref(g, 0xad, g->bytes_work, slot);
  ins(g, 0xe9, 1, 2);
  ref(g, 0x8d, g->bytes_work, slot);
  ref(g, 0xad, g->bytes_work, slot + 1);
  ins(g, 0xe9, 0, 2);
  ref(g, 0x8d, g->bytes_work, slot + 1);
}
static void bytes_copy_word(Backend *g, int from, int to) {
  int j;
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->bytes_work, from + j);
    ref(g, 0x8d, g->bytes_work, to + j);
  }
}
static void bytes_return(Backend *g, int slot) {
  int j;
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->bytes_work, slot + j);
    ins(g, 0x85, R + j, 2);
  }
  ins(g, 0x60, 0, 1);
}
/* binary() has already captured both operands in Python evaluation order. */
static void bytes_binary(Backend *g, Node *n) {
  int j, reverse = g->c->nodes[n->a].type != TY_BYTES;
  bytes_workspace(g);
  if (!g->bytes_copy)
    g->bytes_copy = label(g);
  for (j = 0; j < 2; ++j) {
    if (reverse)
      ins(g, 0xa5, R + j, 2);
    else
      ref(g, 0xad, g->lhs, j);
    ref(g, 0x8d, g->bytes_work, j);
    if (reverse)
      ref(g, 0xad, g->lhs, j);
    else
      ins(g, 0xa5, R + j, 2);
    ref(g, 0x8d, g->bytes_work, j + 12);
  }
  if (n->op == '+') {
    if (!g->bytes_join)
      g->bytes_join = label(g);
    ref(g, 0x20, g->bytes_join, 0);
  } else {
    if (!g->bytes_repeat)
      g->bytes_repeat = label(g);
    ins(g, 0xa9, n->c, 2);
    ref(g, 0x8d, g->bytes_work, 8);
    ref(g, 0x20, g->bytes_repeat, 0);
  }
}
static void bytes_constructor(Backend *g, Node *n) {
  Node *source = &g->c->nodes[n->b];
  int j, data, count;
  if (!n->op) {
    expr(g, n->b);
    return;
  }
  if (!g->bytes_construct)
    g->bytes_construct = label(g);
  bytes_workspace(g);
  if (n->op == 1) {
    expr(g, n->b);
    bytes_store_word(g, 4);
  } else if (n->op == 3) {
    expr(g, n->b);
    bytes_store_word(g, 0);
    bytes_length(g, 0, 4);
  } else if (source->kind == N_LIST) {
    data = snapshot_list(g, n->b);
    set_pointer(g, data, 0);
    for (j = 0; j < 2; ++j) {
      ins(g, 0xa5, P + j, 2);
      ref(g, 0x8d, g->bytes_work, j);
    }
    constant(g, source->d);
    bytes_store_word(g, 4);
  } else {
    Symbol *s = &g->c->symbols[source->symbol];
    if (s->reference) {
      for (j = 0; j < 2; ++j) {
        ref(g, 0xad, s->label, j);
        ref(g, 0x8d, g->bytes_work, j);
        ref(g, 0xad, s->label, j + 2);
        ref(g, 0x8d, g->bytes_work, j + 4);
      }
    } else {
      data = s->label;
      count = s->count;
      set_pointer(g, data, 0);
      for (j = 0; j < 2; ++j) {
        ins(g, 0xa5, P + j, 2);
        ref(g, 0x8d, g->bytes_work, j);
      }
      constant(g, count);
      bytes_store_word(g, 4);
    }
  }
  ins(g, 0xa9,
      n->op == 1                                  ? 0
      : n->op == 3                                ? 1
      : type_size(n->c) == 1 && type_signed(n->c) ? 129
                                                  : type_size(n->c),
      2);
  ref(g, 0x8d, g->bytes_work, 8);
  ref(g, 0x20, g->bytes_construct, 0);
}
/* Capture all operands on the hardware stack before touching shared work. */
static void bytes_slice_expression(Backend *g, Node *n) {
  int parts[4] = {n->a, n->b, n->c, n->d}, slots[4] = {0, 8, 12, 14}, k, j;
  bytes_workspace(g);
  if (!g->bytes_slice)
    g->bytes_slice = label(g);
  for (k = 0; k < 4; ++k) {
    if (parts[k])
      expr(g, parts[k]);
    else
      constant(g, k == 3 ? 1 : 0);
    push(g);
  }
  for (k = 3; k >= 0; --k) {
    pop_lhs(g);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->lhs, j);
      ref(g, 0x8d, g->bytes_work, slots[k] + j);
    }
  }
  ins(g, 0xa9, n->op, 2);
  ref(g, 0x8d, g->bytes_work, 18);
  ref(g, 0x20, g->bytes_slice, 0);
}
/* Normalize an explicitly supplied bound against [0,len] or [-1,len-1].
   A missing reverse stop is -1; an explicit -1 first adds len. */
static void bytes_slice_bound(Backend *g, int slot, int missing,
                              int signed_flag, int start) {
  int clip = label(g), upper = label(g), lower = label(g), omitted = label(g),
      forward = label(g), done = label(g), j;
  ref(g, 0xad, g->bytes_work, 18);
  ins(g, 0x29, missing, 2);
  branch(g, 0xd0, omitted);
  ref(g, 0xad, g->bytes_work, 18);
  ins(g, 0x29, signed_flag, 2);
  branch(g, 0xf0, clip);
  ref(g, 0xad, g->bytes_work, slot + 1);
  branch(g, 0x10, clip);
  ins(g, 0x18, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->bytes_work, slot + j);
    ref(g, 0x6d, g->bytes_work, 16 + j);
    ref(g, 0x8d, g->bytes_work, slot + j);
  }
  ref(g, 0xad, g->bytes_work, slot + 1);
  branch(g, 0x30, lower);
  bind(g, clip);
  ref(g, 0xad, g->bytes_work, slot + 1);
  ref(g, 0xcd, g->bytes_work, 23);
  branch(g, 0x90, done);
  branch(g, 0xd0, upper);
  ref(g, 0xad, g->bytes_work, slot);
  ref(g, 0xcd, g->bytes_work, 22);
  branch(g, 0x90, done);
  bind(g, upper);
  bytes_copy_word(g, 22, slot);
  jump(g, done);
  bind(g, lower);
  ref(g, 0xad, g->bytes_work, 20);
  ins(g, 0x49, 1, 2);
  ins(g, 0x38, 0, 1);
  ins(g, 0xe9, 1, 2); /* Forward lower=0, reverse lower=$FFFF. */
  ref(g, 0x8d, g->bytes_work, slot);
  ref(g, 0x8d, g->bytes_work, slot + 1);
  jump(g, done);
  bind(g, omitted);
  ref(g, 0xad, g->bytes_work, 20);
  branch(g, 0xf0, forward);
  if (start)
    bytes_copy_word(g, 22, slot);
  else {
    ins(g, 0xa9, 255, 2);
    ref(g, 0x8d, g->bytes_work, slot);
    ref(g, 0x8d, g->bytes_work, slot + 1);
  }
  jump(g, done);
  bind(g, forward);
  if (start) {
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->bytes_work, slot);
    ref(g, 0x8d, g->bytes_work, slot + 1);
  } else
    bytes_copy_word(g, 16, slot);
  bind(g, done);
}
static void emit_bytes_slice(Backend *g) {
  int work = g->bytes_work, forward = label(g), normalized = label(g),
      allocate = label(g), reverse = label(g), difference = label(g),
      numerator = label(g), loop = label(g), subtract = label(g),
      advanced = label(g), done = label(g), identity = label(g), j;
  bind(g, g->bytes_slice);
  bytes_length(g, 0, 16);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, work, 4);
  ref(g, 0x8d, work, 5);
  ref(g, 0x8d, work, 20);
  ref(g, 0xad, work, 18);
  ins(g, 0x29, 16, 2);
  branch(g, 0xf0, forward);
  ref(g, 0xad, work, 15);
  branch(g, 0x10, forward);
  ins(g, 0xa9, 1, 2);
  ref(g, 0x8d, work, 20);
  ins(g, 0x38, 0, 1);
  for (j = 0; j < 2; ++j) {
    ins(g, 0xa9, 0, 2);
    ref(g, 0xed, work, 14 + j);
    ref(g, 0x8d, work, 14 + j);
  }
  bind(g, forward);
  ref(g, 0xad, work, 14);
  ref(g, 0x0d, work, 15);
  branch(g, 0xf0, g->trap);
  ref(g, 0xad, work, 16);
  ref(g, 0x0d, work, 17);
  branch(g, 0xf0, allocate);
  bytes_copy_word(g, 16, 22);
  ref(g, 0xad, work, 20);
  branch(g, 0xf0, normalized);
  bytes_decrement(g, 22);
  bind(g, normalized);
  bytes_slice_bound(g, 8, 1, 4, 1);
  bytes_slice_bound(g, 12, 2, 8, 0);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 8 + j);
    ref(g, 0x8d, g->lhs, j);
    ref(g, 0xad, work, 12 + j);
    ins(g, 0x85, R + j, 2);
  }
  ref(g, 0xad, work, 20);
  branch(g, 0xd0, reverse);
  compare(g, '<', 1);
  ins(g, 0xa5, R, 2);
  branch(g, 0xf0, allocate);
  /* Full forward slices may safely share an immutable source. */
  ref(g, 0xad, work, 8);
  ref(g, 0x0d, work, 9);
  branch(g, 0xd0, difference);
  ref(g, 0xad, work, 15);
  branch(g, 0xd0, difference);
  ref(g, 0xad, work, 14);
  ins(g, 0xc9, 1, 2);
  branch(g, 0xd0, difference);
  ref(g, 0xad, work, 12);
  ref(g, 0xcd, work, 16);
  branch(g, 0xd0, difference);
  ref(g, 0xad, work, 13);
  ref(g, 0xcd, work, 17);
  branch(g, 0xf0, identity);
  bind(g, difference);
  ins(g, 0x38, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 12 + j);
    ref(g, 0xed, work, 8 + j);
    ref(g, 0x8d, work, 22 + j);
  }
  jump(g, numerator);
  bind(g, reverse);
  compare(g, '>', 1);
  ins(g, 0xa5, R, 2);
  branch(g, 0xf0, allocate);
  ins(g, 0x38, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 8 + j);
    ref(g, 0xed, work, 12 + j);
    ref(g, 0x8d, work, 22 + j);
  }
  bind(g, numerator);
  bytes_decrement(g, 22);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 22 + j);
    ref(g, 0x8d, g->lhs, j);
    ref(g, 0xad, work, 14 + j);
    ins(g, 0x85, R + j, 2);
  }
  divide(g, 0);
  ins(g, 0x18, 0, 1);
  ins(g, 0xa5, R, 2);
  ins(g, 0x69, 1, 2);
  ref(g, 0x8d, work, 4);
  ins(g, 0xa5, RH, 2);
  ins(g, 0x69, 0, 2);
  ref(g, 0x8d, work, 5);
  bind(g, allocate);
  ref(g, 0x20, g->bytes_allocate, 0);
  bind(g, loop);
  ref(g, 0xad, work, 4);
  ref(g, 0x0d, work, 5);
  branch(g, 0xf0, done);
  ins(g, 0x18, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, j);
    ref(g, 0x6d, work, 8 + j);
    ins(g, 0x85, P + j, 2);
  }
  ins(g, 0xa0, 0, 2);
  ins(g, 0xb1, P, 2);
  ref(g, 0x8d, work, 24);
  bytes_pointer(g, 2);
  ins(g, 0xa0, 0, 2);
  ref(g, 0xad, work, 24);
  ins(g, 0x91, P, 2);
  bytes_advance(g, 2, 1);
  bytes_decrement(g, 4);
  ref(g, 0xad, work, 4);
  ref(g, 0x0d, work, 5);
  branch(g, 0xf0, done);
  ref(g, 0xad, work, 20);
  branch(g, 0xd0, subtract);
  ins(g, 0x18, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 8 + j);
    ref(g, 0x6d, work, 14 + j);
    ref(g, 0x8d, work, 8 + j);
  }
  jump(g, advanced);
  bind(g, subtract);
  ins(g, 0x38, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 8 + j);
    ref(g, 0xed, work, 14 + j);
    ref(g, 0x8d, work, 8 + j);
  }
  bind(g, advanced);
  jump(g, loop);
  bind(g, done);
  bytes_return(g, 10);
  bind(g, identity);
  bytes_return(g, 0);
}
static void bytes_hex_digit(Backend *g) {
  int number = label(g), done = label(g);
  ins(g, 0xc9, 10, 2);
  branch(g, 0x90, number);
  ins(g, 0x18, 0, 1);
  ins(g, 0x69, 'A' - 10, 2);
  jump(g, done);
  bind(g, number);
  ins(g, 0x18, 0, 1);
  ins(g, 0x69, '0', 2);
  bind(g, done);
  ins(g, 0x20, 0xffd2, 3);
}
static void emit_bytes_helpers(Backend *g) {
  int work = g->bytes_work, j;
  if (g->bytes_search)
    emit_bytes_search(g);
  if (g->bytes_slice)
    emit_bytes_slice(g);
  if (g->bytes_compare) {
    int loop = label(g), left_empty = label(g), equal = label(g),
        less = label(g), greater = label(g);
    bind(g, g->bytes_compare);
    bytes_length(g, 0, 4);
    bytes_length(g, 2, 6);
    bind(g, loop);
    ref(g, 0xad, work, 4);
    ref(g, 0x0d, work, 5);
    branch(g, 0xf0, left_empty);
    ref(g, 0xad, work, 6);
    ref(g, 0x0d, work, 7);
    branch(g, 0xf0, greater);
    bytes_pointer(g, 0);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ref(g, 0x8d, work, 9);
    bytes_pointer(g, 2);
    ref(g, 0xad, work, 9);
    ins(g, 0xd1, P, 2);
    branch(g, 0x90, less);
    branch(g, 0xd0, greater);
    bytes_advance(g, 0, 1);
    bytes_advance(g, 2, 1);
    bytes_decrement(g, 4);
    bytes_decrement(g, 6);
    jump(g, loop);
    bind(g, left_empty);
    ref(g, 0xad, work, 6);
    ref(g, 0x0d, work, 7);
    branch(g, 0xf0, equal);
    bind(g, less);
    constant(g, -1);
    ins(g, 0x60, 0, 1);
    bind(g, greater);
    constant(g, 1);
    ins(g, 0x60, 0, 1);
    bind(g, equal);
    constant(g, 0);
    ins(g, 0x60, 0, 1);
  }
  if (g->print_bytes) {
    int scan = label(g), scanned = label(g), advance = label(g),
        choose = label(g), loop = label(g), done = label(g), literal = label(g),
        escaped = label(g), hex = label(g), next = label(g), newline = label(g),
        carriage = label(g), tab = label(g);
    bind(g, g->print_bytes);
    bytes_store_word(g, 0);
    bytes_store_word(g, 2); /* Retain the original value for the print pass. */
    bytes_length(g, 0, 4);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, work, 10);
    bind(g, scan);
    ref(g, 0xad, work, 4);
    ref(g, 0x0d, work, 5);
    branch(g, 0xf0, scanned);
    bytes_pointer(g, 0);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ins(g, 0xc9, '\'', 2);
    branch(g, 0xd0, choose);
    ref(g, 0xad, work, 10);
    ins(g, 0x09, 1, 2);
    ref(g, 0x8d, work, 10);
    jump(g, advance);
    bind(g, choose);
    ins(g, 0xc9, '"', 2);
    branch(g, 0xd0, advance);
    ref(g, 0xad, work, 10);
    ins(g, 0x09, 2, 2);
    ref(g, 0x8d, work, 10);
    bind(g, advance);
    bytes_advance(g, 0, 1);
    bytes_decrement(g, 4);
    jump(g, scan);
    bind(g, scanned);
    {
      int single = label(g);
      ref(g, 0xad, work, 10);
      ins(g, 0xc9, 1, 2);
      branch(g, 0xd0, single);
      ins(g, 0xa9, '"', 2);
      jump(g, loop);
      bind(g, single);
      ins(g, 0xa9, '\'', 2);
    }
    bind(g, loop);
    ref(g, 0x8d, work, 10); /* Chosen representation quote. */
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, work, j + 2);
      ref(g, 0x8d, work, j);
    }
    bytes_length(g, 0, 4);
    output_character(g, 'B');
    ref(g, 0xad, work, 10);
    ins(g, 0x20, 0xffd2, 3);
    {
      int body = label(g);
      bind(g, body);
      ref(g, 0xad, work, 4);
      ref(g, 0x0d, work, 5);
      branch(g, 0xf0, done);
      bytes_pointer(g, 0);
      ins(g, 0xa0, 0, 2);
      ins(g, 0xb1, P, 2);
      ref(g, 0x8d, work, 9);
      ins(g, 0xc9, '\\', 2);
      branch(g, 0xf0, escaped);
      ref(g, 0xcd, work, 10);
      branch(g, 0xf0, escaped);
      ins(g, 0xc9, 10, 2);
      branch(g, 0xf0, newline);
      ins(g, 0xc9, 13, 2);
      branch(g, 0xf0, carriage);
      ins(g, 0xc9, 9, 2);
      branch(g, 0xf0, tab);
      ins(g, 0xc9, 32, 2);
      branch(g, 0x90, hex);
      ins(g, 0xc9, 127, 2);
      branch(g, 0x90, literal);
      bind(g, hex);
      output_text(g, "\\x");
      ref(g, 0xad, work, 9);
      for (j = 0; j < 4; ++j)
        ins(g, 0x4a, 0, 1);
      bytes_hex_digit(g);
      ref(g, 0xad, work, 9);
      ins(g, 0x29, 15, 2);
      bytes_hex_digit(g);
      jump(g, next);
      bind(g, escaped);
      output_character(g, '\\');
      ref(g, 0xad, work, 9);
      jump(g, literal);
      bind(g, newline);
      output_text(g, "\\n");
      jump(g, next);
      bind(g, carriage);
      output_text(g, "\\r");
      jump(g, next);
      bind(g, tab);
      output_text(g, "\\t");
      jump(g, next);
      bind(g, literal);
      to_petscii(g);
      ins(g, 0x20, 0xffd2, 3);
      bind(g, next);
      bytes_advance(g, 0, 1);
      bytes_decrement(g, 4);
      jump(g, body);
    }
    bind(g, done);
    ref(g, 0xad, work, 10);
    ins(g, 0x20, 0xffd2, 3);
    ins(g, 0x60, 0, 1);
  }
  if (g->bytes_allocate) {
    int empty = label(g), valid = label(g);
    bind(g, g->bytes_allocate);
    ref(g, 0xad, work, 4);
    ref(g, 0x0d, work, 5);
    branch(g, 0xf0, empty);
    ins(g, 0x18, 0, 1);
    ref(g, 0xad, work, 4);
    ref(g, 0x6d, g->bytes_used, 0);
    ref(g, 0x8d, work, 6);
    ref(g, 0xad, work, 5);
    ref(g, 0x6d, g->bytes_used, 1);
    ref(g, 0x8d, work, 7);
    branch(g, 0xb0, g->trap);
    bytes_advance(g, 6, 2);
    branch(g, 0xb0, g->trap);
    ref(g, 0xad, work, 7);
    ins(g, 0xc9, 16, 2);
    branch(g, 0x90, valid);
    branch(g, 0xd0, g->trap);
    ref(g, 0xad, work, 6);
    branch(g, 0xd0, g->trap);
    bind(g, valid);
    set_pointer(g, g->bytes_pool, 0);
    ins(g, 0x18, 0, 1);
    ins(g, 0xa5, P, 2);
    ref(g, 0x6d, g->bytes_used, 0);
    ref(g, 0x8d, work, 2);
    ins(g, 0x85, P, 2);
    ins(g, 0xa5, PH, 2);
    ref(g, 0x6d, g->bytes_used, 1);
    ref(g, 0x8d, work, 3);
    ins(g, 0x85, PH, 2);
    ins(g, 0xa0, 0, 2);
    ref(g, 0xad, work, 4);
    ins(g, 0x91, P, 2);
    ins(g, 0xc8, 0, 1);
    ref(g, 0xad, work, 5);
    ins(g, 0x91, P, 2);
    bytes_advance(g, 2, 2);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, work, j + 2);
      ref(g, 0x8d, work, j + 10);
      ref(g, 0xad, work, j + 6);
      ref(g, 0x8d, g->bytes_used, j);
    }
    bytes_return(g, 10);
    bind(g, empty);
    set_pointer(g, g->bytes_label, 2);
    for (j = 0; j < 2; ++j) {
      ins(g, 0xa5, P + j, 2);
      ref(g, 0x8d, work, j + 2);
      ref(g, 0x8d, work, j + 10);
    }
    bytes_return(g, 10);
  }
  if (g->bytes_construct) {
    int loop = label(g), zero = label(g), byte = label(g), ready = label(g),
        done = label(g), advanced = label(g);
    bind(g, g->bytes_construct);
    ref(g, 0x20, g->bytes_allocate, 0);
    bind(g, loop);
    ref(g, 0xad, work, 4);
    ref(g, 0x0d, work, 5);
    branch(g, 0xf0, done);
    ref(g, 0xad, work, 8);
    branch(g, 0xf0, zero);
    bytes_pointer(g, 0);
    ref(g, 0xad, work, 8);
    ins(g, 0xc9, 2, 2);
    branch(g, 0xd0, byte);
    ins(g, 0xa0, 1, 2);
    ins(g, 0xb1, P, 2);
    branch(g, 0xd0, g->trap);
    bind(g, byte);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    jump(g, ready);
    bind(g, zero);
    ins(g, 0xa9, 0, 2);
    bind(g, ready);
    ref(g, 0x8d, work, 9);
    {
      int valid_byte = label(g);
      ref(g, 0xad, work, 8);
      branch(g, 0x10, valid_byte);
      ref(g, 0xad, work, 9);
      branch(g, 0x30, g->trap);
      bind(g, valid_byte);
    }
    bytes_pointer(g, 2);
    ins(g, 0xa0, 0, 2);
    ref(g, 0xad, work, 9);
    ins(g, 0x91, P, 2);
    bytes_advance(g, 2, 1);
    ref(g, 0xad, work, 8);
    ins(g, 0x29, 3, 2);
    branch(g, 0xf0, advanced);
    ins(g, 0x18, 0, 1);
    ref(g, 0x6d, work, 0);
    ref(g, 0x8d, work, 0);
    ref(g, 0xad, work, 1);
    ins(g, 0x69, 0, 2);
    ref(g, 0x8d, work, 1);
    bind(g, advanced);
    bytes_decrement(g, 4);
    jump(g, loop);
    bind(g, done);
    bytes_return(g, 10);
  }
  if (g->bytes_copy) {
    int loop = label(g), done = label(g);
    bind(g, g->bytes_copy);
    bind(g, loop);
    ref(g, 0xad, work, 4);
    ref(g, 0x0d, work, 5);
    branch(g, 0xf0, done);
    bytes_pointer(g, 0);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ref(g, 0x8d, work, 9);
    bytes_pointer(g, 2);
    ins(g, 0xa0, 0, 2);
    ref(g, 0xad, work, 9);
    ins(g, 0x91, P, 2);
    bytes_advance(g, 0, 1);
    bytes_advance(g, 2, 1);
    bytes_decrement(g, 4);
    jump(g, loop);
    bind(g, done);
    ins(g, 0x60, 0, 1);
  }
  if (g->bytes_join) {
    int left_empty = label(g), right_empty = label(g);
    bind(g, g->bytes_join);
    bytes_length(g, 0, 16);
    bytes_length(g, 12, 14);
    ref(g, 0xad, work, 16);
    ref(g, 0x0d, work, 17);
    branch(g, 0xf0, left_empty);
    ref(g, 0xad, work, 14);
    ref(g, 0x0d, work, 15);
    branch(g, 0xf0, right_empty);
    ins(g, 0x18, 0, 1);
    ref(g, 0xad, work, 16);
    ref(g, 0x6d, work, 14);
    ref(g, 0x8d, work, 4);
    ref(g, 0xad, work, 17);
    ref(g, 0x6d, work, 15);
    ref(g, 0x8d, work, 5);
    branch(g, 0xb0, g->trap);
    ref(g, 0x20, g->bytes_allocate, 0);
    bytes_copy_word(g, 16, 4);
    ref(g, 0x20, g->bytes_copy, 0);
    bytes_copy_word(g, 12, 0);
    bytes_copy_word(g, 14, 4);
    ref(g, 0x20, g->bytes_copy, 0);
    bytes_return(g, 10);
    bind(g, left_empty);
    bytes_return(g, 12);
    bind(g, right_empty);
    bytes_return(g, 0);
  }
  if (g->bytes_repeat) {
    int nonnegative = label(g), calculate = label(g), allocate = label(g),
        sum = label(g), valid = label(g), copy = label(g), done = label(g),
        identity = label(g);
    bind(g, g->bytes_repeat);
    bytes_length(g, 0, 16);
    bytes_copy_word(g, 0, 18);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, work, 4);
    ref(g, 0x8d, work, 5);
    ref(g, 0xad, work, 8);
    branch(g, 0xf0, nonnegative);
    ref(g, 0xad, work, 13);
    branch(g, 0x30, allocate); /* Signed negative repetitions are empty. */
    bind(g, nonnegative);
    ref(g, 0xad, work, 16);
    ref(g, 0x0d, work, 17);
    branch(g, 0xf0, allocate);
    ref(g, 0xad, work, 13);
    branch(g, 0xd0, calculate);
    ref(g, 0xad, work, 12);
    ins(g, 0xc9, 1, 2);
    branch(g, 0xf0, identity);
    bind(g, calculate);
    bytes_copy_word(g, 12, 14);
    bind(g, sum);
    ref(g, 0xad, work, 14);
    ref(g, 0x0d, work, 15);
    branch(g, 0xf0, allocate);
    ins(g, 0x18, 0, 1);
    ref(g, 0xad, work, 4);
    ref(g, 0x6d, work, 16);
    ref(g, 0x8d, work, 4);
    ref(g, 0xad, work, 5);
    ref(g, 0x6d, work, 17);
    ref(g, 0x8d, work, 5);
    branch(g, 0xb0, g->trap);
    ins(g, 0xc9, 15, 2);
    branch(g, 0x90, valid);
    branch(g, 0xd0, g->trap);
    ref(g, 0xad, work, 4);
    ins(g, 0xc9, 255, 2);
    branch(g, 0xb0, g->trap);
    bind(g, valid);
    bytes_decrement(g, 14);
    jump(g, sum);
    bind(g, allocate);
    ref(g, 0x20, g->bytes_allocate, 0);
    ref(g, 0xad, work, 4);
    ref(g, 0x0d, work, 5);
    branch(g, 0xf0, done);
    bind(g, copy);
    bytes_copy_word(g, 18, 0);
    bytes_copy_word(g, 16, 4);
    ref(g, 0x20, g->bytes_copy, 0);
    bytes_decrement(g, 12);
    ref(g, 0xad, work, 12);
    ref(g, 0x0d, work, 13);
    branch(g, 0xd0, copy);
    bind(g, done);
    bytes_return(g, 10);
    bind(g, identity);
    bytes_return(g, 0);
  }
}

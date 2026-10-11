/* All arguments are captured before searching length-prefixed binary values.
   Searches use bounded work storage and never allocate immutable results. */
static void bytes_membership_expression(Backend *g, Node *n) {
  int j, k, parts[2] = {n->a, n->b}, slots[2] = {2, 0};
  int absent = label(g), done = label(g);
  bytes_workspace(g);
  if (!g->bytes_search) {
    g->bytes_search = label(g);
    g->bytes_match = label(g);
  }
  /* Membership evaluates the needle before the container, unlike a method. */
  for (k = 0; k < 2; ++k) {
    expr(g, parts[k]);
    push(g);
  }
  for (k = 1; k >= 0; --k) {
    pop_lhs(g);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->lhs, j);
      ref(g, 0x8d, g->bytes_work, slots[k] + j);
    }
  }
  ins(g, 0xa9, 6 | n->c, 2);
  ref(g, 0x8d, g->bytes_work, 18);
  ins(g, 0xa9, BS_FIND, 2);
  ref(g, 0x8d, g->bytes_work, 20);
  ref(g, 0x20, g->bytes_search, 0);
  ins(g, 0xa5, RH, 2);
  ins(g, 0xc9, 255, 2); /* A found position is nonnegative; missing is -1. */
  branch(g, 0xf0, absent);
  constant(g, n->op == OP_IN);
  jump(g, done);
  bind(g, absent);
  constant(g, n->op == OP_NOTIN);
  bind(g, done);
}
static void bytes_search_expression(Backend *g, Node *n) {
  int parts[4] = {n->a, n->b, 0, 0}, slots[4] = {0, 2, 8, 10}, k, j;
  parts[2] = g->c->nodes[n->b].next;
  parts[3] = g->c->nodes[parts[2]].next;
  bytes_workspace(g);
  if (!g->bytes_search) {
    g->bytes_search = label(g);
    g->bytes_match = label(g);
  }
  for (k = 0; k < 4; ++k) {
    if (parts[k] && g->c->nodes[parts[k]].kind != N_NONE)
      expr(g, parts[k]);
    else
      constant(g, 0);
    push(g);
  }
  for (k = 3; k >= 0; --k) {
    pop_lhs(g);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->lhs, j);
      ref(g, 0x8d, g->bytes_work, slots[k] + j);
    }
  }
  ins(g, 0xa9, n->c, 2);
  ref(g, 0x8d, g->bytes_work, 18);
  ins(g, 0xa9, n->op, 2);
  ref(g, 0x8d, g->bytes_work, 20);
  ref(g, 0x20, g->bytes_search, 0);
}
static void bytes_search_compare(Backend *g, int left, int right, int op) {
  int j;
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->bytes_work, left + j);
    ref(g, 0x8d, g->lhs, j);
    ref(g, 0xad, g->bytes_work, right + j);
    ins(g, 0x85, R + j, 2);
  }
  compare(g, op, 0);
}
static void bytes_search_bound(Backend *g, int slot, int missing,
                               int signed_flag, int end) {
  int supplied = label(g), clip = label(g), lower = label(g), done = label(g),
      j;
  ref(g, 0xad, g->bytes_work, 18);
  ins(g, 0x29, missing, 2);
  branch(g, 0xf0, supplied);
  if (end)
    bytes_copy_word(g, 4, slot);
  else {
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->bytes_work, slot);
    ref(g, 0x8d, g->bytes_work, slot + 1);
  }
  jump(g, done);
  bind(g, supplied);
  ref(g, 0xad, g->bytes_work, 18);
  ins(g, 0x29, signed_flag, 2);
  branch(g, 0xf0, clip);
  ref(g, 0xad, g->bytes_work, slot + 1);
  branch(g, 0x10, clip);
  ins(g, 0x18, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->bytes_work, slot + j);
    ref(g, 0x6d, g->bytes_work, 4 + j);
    ref(g, 0x8d, g->bytes_work, slot + j);
  }
  ref(g, 0xad, g->bytes_work, slot + 1);
  branch(g, 0x30, lower);
  bind(g, clip);
  if (end) {
    bytes_search_compare(g, slot, 4, T_LE);
    ins(g, 0xa5, R, 2);
    branch(g, 0xd0, done);
    bytes_copy_word(g, 4, slot);
  }
  jump(g, done);
  bind(g, lower);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->bytes_work, slot);
  ref(g, 0x8d, g->bytes_work, slot + 1);
  bind(g, done);
}
static void emit_bytes_search(Backend *g) {
  int work = g->bytes_work, j;
  int binary = label(g), setup = label(g), absent = label(g), empty = label(g),
      last = label(g), loop = label(g), found = label(g), forward = label(g),
      advance = label(g), bounds = label(g), counted = label(g),
      count_result = label(g), reverse_empty = label(g), empty_count = label(g),
      empty_bool = label(g), false_result = label(g);
  bind(g, g->bytes_search);
  bytes_length(g, 0, 4);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, work, 14);
  ref(g, 0x8d, work, 15);
  ref(g, 0xad, work, 18);
  ins(g, 0x29, 1, 2);
  branch(g, 0xf0, binary);
  ref(g, 0xad, work, 3);
  branch(g, 0xd0, g->trap);
  ins(g, 0xa9, 1, 2);
  ref(g, 0x8d, work, 6);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, work, 7);
  jump(g, setup);
  bind(g, binary);
  bytes_length(g, 2, 6);
  bind(g, setup);
  bytes_search_bound(g, 8, 2, 8, 0);
  bytes_search_bound(g, 10, 4, 16, 1);
  bytes_search_compare(g, 8, 4, '>');
  ins(g, 0xa5, R, 2);
  branch(g, 0xd0,
         absent); /* Preserve starts beyond len, even for empty needles. */
  bytes_search_compare(g, 8, 10, '>');
  ins(g, 0xa5, R, 2);
  branch(g, 0xd0, absent);
  ins(g, 0x38, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 10 + j);
    ref(g, 0xed, work, 8 + j);
    ref(g, 0x8d, work, 16 + j);
  }
  bytes_search_compare(g, 6, 16, '>');
  ins(g, 0xa5, R, 2);
  branch(g, 0xd0, absent);
  ref(g, 0xad, work, 6);
  ref(g, 0x0d, work, 7);
  branch(g, 0xf0, empty);
  ins(g, 0x38, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 10 + j);
    ref(g, 0xed, work, 6 + j);
    ref(g, 0x8d, work, 22 + j);
  }
  bytes_copy_word(g, 8, 12);
  ref(g, 0xad, work, 20);
  ins(g, 0xc9, BS_RFIND, 2);
  branch(g, 0xf0, last);
  ins(g, 0xc9, BS_RINDEX, 2);
  branch(g, 0xf0, last);
  ins(g, 0xc9, BS_SUFFIX, 2);
  branch(g, 0xd0, loop);
  bind(g, last);
  bytes_copy_word(g, 22, 12);
  bind(g, loop);
  ref(g, 0x20, g->bytes_match, 0);
  ins(g, 0xa5, R, 2);
  branch(g, 0xd0, found);
  ref(g, 0xad, work, 20);
  ins(g, 0xc9, BS_PREFIX, 2);
  branch(g, 0xb0, false_result);
  ins(g, 0xc9, BS_RFIND, 2);
  branch(g, 0xf0, advance);
  ins(g, 0xc9, BS_RINDEX, 2);
  branch(g, 0xd0, forward);
  bind(g, advance);
  bytes_search_compare(g, 12, 8, T_EQ);
  ins(g, 0xa5, R, 2);
  branch(g, 0xd0, absent);
  bytes_decrement(g, 12);
  jump(g, loop);
  bind(g, forward);
  bytes_advance(g, 12, 1);
  jump(g, bounds);
  bind(g, found);
  ref(g, 0xad, work, 20);
  ins(g, 0xc9, BS_COUNT, 2);
  branch(g, 0xf0, counted);
  ins(g, 0xc9, BS_PREFIX, 2);
  branch(g, 0xb0, empty_bool);
  bytes_return(g, 12);
  bind(g, counted);
  bytes_advance(g, 14, 1);
  ins(g, 0x18, 0, 1);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, work, 12 + j);
    ref(g, 0x6d, work, 6 + j);
    ref(g, 0x8d, work, 12 + j);
  }
  bind(g, bounds);
  bytes_search_compare(g, 12, 22, T_LE);
  ins(g, 0xa5, R, 2);
  branch(g, 0xd0, loop);
  bind(g, absent);
  ref(g, 0xad, work, 20);
  ins(g, 0xc9, BS_INDEX, 2);
  branch(g, 0xf0, g->trap);
  ins(g, 0xc9, BS_RINDEX, 2);
  branch(g, 0xf0, g->trap);
  ins(g, 0xc9, BS_COUNT, 2);
  branch(g, 0xf0, count_result);
  ins(g, 0xc9, BS_PREFIX, 2);
  branch(g, 0xb0, false_result);
  constant(g, -1);
  ins(g, 0x60, 0, 1);
  bind(g, count_result);
  bytes_return(g, 14);
  bind(g, false_result);
  constant(g, 0);
  ins(g, 0x60, 0, 1);
  bind(g, empty);
  ref(g, 0xad, work, 20);
  ins(g, 0xc9, BS_COUNT, 2);
  branch(g, 0xf0, empty_count);
  ins(g, 0xc9, BS_PREFIX, 2);
  branch(g, 0xb0, empty_bool);
  ins(g, 0xc9, BS_RFIND, 2);
  branch(g, 0xf0, reverse_empty);
  ins(g, 0xc9, BS_RINDEX, 2);
  branch(g, 0xf0, reverse_empty);
  bytes_return(g, 8);
  bind(g, reverse_empty);
  bytes_return(g, 10);
  bind(g, empty_count);
  bytes_copy_word(g, 16, 14);
  bytes_advance(g, 14, 1);
  bytes_return(g, 14);
  bind(g, empty_bool);
  constant(g, 1);
  ins(g, 0x60, 0, 1);
  {
    int match_loop = label(g), mismatch = label(g), matched = label(g),
        needle_bytes = label(g), same = label(g);
    bind(g, g->bytes_match);
    ins(g, 0x18, 0, 1);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, work, j);
      ref(g, 0x6d, work, 12 + j);
      ref(g, 0x8d, work, 24 + j);
    }
    bytes_copy_word(g, 2, 26);
    bytes_copy_word(g, 6, 28);
    bind(g, match_loop);
    ref(g, 0xad, work, 28);
    ref(g, 0x0d, work, 29);
    branch(g, 0xf0, matched);
    bytes_pointer(g, 24);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ref(g, 0x8d, work, 30);
    ref(g, 0xad, work, 18);
    ins(g, 0x29, 1, 2);
    branch(g, 0xf0, needle_bytes);
    ref(g, 0xad, work, 30);
    ref(g, 0xcd, work, 2);
    branch(g, 0xd0, mismatch);
    jump(g, same);
    bind(g, needle_bytes);
    bytes_pointer(g, 26);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ref(g, 0xcd, work, 30);
    branch(g, 0xd0, mismatch);
    bind(g, same);
    bytes_advance(g, 24, 1);
    bytes_advance(g, 26, 1);
    bytes_decrement(g, 28);
    jump(g, match_loop);
    bind(g, mismatch);
    constant(g, 0);
    ins(g, 0x60, 0, 1);
    bind(g, matched);
    constant(g, 1);
    ins(g, 0x60, 0, 1);
  }
}

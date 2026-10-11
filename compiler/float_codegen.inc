/* Native C64 float values occupy five bytes; expressions carry their address
   in R/RH. Snapshot the value itself whenever later evaluation can mutate it.
 */
static void float_push(Backend *g) {
  int j;
  ins(g, 0xa5, R, 2);
  ins(g, 0x85, P, 2);
  ins(g, 0xa5, RH, 2);
  ins(g, 0x85, PH, 2);
  for (j = 0; j < 5; ++j) {
    ins(g, 0xa0, j, 2);
    ins(g, 0xb1, P, 2);
    ins(g, 0x48, 0, 1);
  }
}
static void float_pop(Backend *g, int destination, int offset) {
  int j;
  for (j = 4; j >= 0; --j) {
    ins(g, 0x68, 0, 1);
    ref(g, 0x8d, destination, offset + j);
  }
}
static void float_copy(Backend *g, int destination, int offset) {
  int j;
  ins(g, 0xa5, R, 2);
  ins(g, 0x85, P, 2);
  ins(g, 0xa5, RH, 2);
  ins(g, 0x85, PH, 2);
  for (j = 0; j < 5; ++j) {
    ins(g, 0xa0, j, 2);
    ins(g, 0xb1, P, 2);
    ref(g, 0x8d, destination, offset + j);
  }
}
static void float_pointer(Backend *g, int destination, int offset) {
  set_pointer(g, destination, offset);
  ins(g, 0xa5, P, 2);
  ins(g, 0x85, R, 2);
  ins(g, 0xa5, PH, 2);
  ins(g, 0x85, RH, 2);
}
static void float_fac(Backend *g, int type) {
  ins(g, 0xa9, 0, 2);
  ins(g, 0x85, 0x68, 2); /* FAC overflow byte is caller-owned. */
  if (type == TY_FLOAT) {
    ins(g, 0xa5, R, 2);
    ins(g, 0xa4, RH, 2);
    ins(g, 0x20, 0xbba2, 3);
  } else {
    ins(g, 0xa5, RH, 2);
    ins(g, 0xa4, R, 2);
    ins(g, 0x20, 0xb391, 3);
    if (integer_type(type) == TY_U16) {
      int done = label(g);
      ins(g, 0xa5, RH, 2);
      branch(g, 0x10, done);
      set_pointer(g, g->float_65536, 0);
      ins(g, 0xa5, P, 2);
      ins(g, 0xa4, PH, 2);
      ins(g, 0x20, 0xb867, 3);
      bind(g, done);
    }
  }
}
static void float_finish(Backend *g) {
  set_pointer(g, g->float_result, 0);
  ins(g, 0xa6, P, 2);
  ins(g, 0xa4, PH, 2);
  ins(g, 0x20, 0xbbd4, 3);
  float_pointer(g, g->float_result, 0);
}
static void float_parse_text(Backend *g, int integer) {
  if (!g->float_parse)
    g->float_parse = label(g);
  ins(g, 0xa9, integer, 2);
  ref(g, 0x8d, g->float_state,
      151); /* Restrict int(text) to decimal integers. */
  ref(g, 0x20, g->float_parse, 0);
}
static void float_convert(Backend *g, int i) {
  expr(g, i);
  if (g->c->nodes[i].type == TY_STR)
    float_parse_text(g, 0);
  else
    float_fac(g, g->c->nodes[i].type);
  float_finish(g);
}

static void float_integer_convert(Backend *g, int bounded) {
  int valid = label(g);
  if (!g->float_to_integer) {
    g->float_to_integer = label(g);
    g->float_integer_work = label(g);
  }
  ins(g, 0xa5, R, 2);
  ins(g, 0x85, P, 2);
  ins(g, 0xa5, RH, 2);
  ins(g, 0x85, PH, 2);
  if (bounded) {
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ins(g, 0xc9, 144, 2);
    branch(g, 0x90, valid);
    branch(g, 0xd0, g->trap);
    ins(g, 0xc8, 0, 1);
    ins(g, 0xb1, P, 2);
    branch(g, 0x10, g->trap);
    ins(g, 0x29, 127, 2);
    branch(g, 0xd0, g->trap);
    /* Negative magnitudes in [32768,32769) truncate to -32768. */
    ins(g, 0xa0, 2, 2);
    ins(g, 0xb1, P, 2);
    branch(g, 0xd0, g->trap);
  }
  bind(g, valid);
  ref(g, 0x20, g->float_to_integer, 0);
}
#include "float_divmod.inc"

static void float_binary(Backend *g, Node *n) {
  int op = n->op;
  expr(g, n->a);
  float_push(g);
  expr(g, n->b);
  if (op == T_FLOORDIV || op == '%') {
    if (!g->float_divmod) {
      g->float_divmod = label(g);
      g->float_divmod_work = label(g);
    }
    float_copy(g, g->float_divmod_work, 0);
    float_pop(g, g->float_left, 0);
    ins(g, 0xa9, op == '%', 2);
    ref(g, 0x20, g->float_divmod, 0);
    float_pointer(g, g->float_result, 0);
    return;
  }
  float_fac(g, TY_FLOAT);
  float_pop(g, g->float_left, 0);
  set_pointer(g, g->float_left, 0);
  ins(g, 0xa5, P, 2);
  ins(g, 0xa4, PH, 2);
  if (op == T_POWER) {
    int valid = label(g);
    /* BASIC's power routine returns zero for 0**negative; Python rejects it. */
    ref(g, 0xad, g->float_left, 0);
    branch(g, 0xd0, valid);
    ins(g, 0xa5, 0x66, 2);
    branch(g, 0x30, g->trap);
    bind(g, valid);
    set_pointer(g, g->float_left, 0);
    ins(g, 0xa5, P, 2);
    ins(g, 0xa4, PH, 2);
    ins(g, 0x20, 0xba8c, 3); /* Memory base -> ARG, exponent stays in FAC. */
    ins(g, 0x20, 0xbf7b, 3);
    float_finish(g);
  } else if (op == '+' || op == '-' || op == '*' || op == '/') {
    ins(g, 0x20,
        op == '+'   ? 0xb867
        : op == '-' ? 0xb850
        : op == '*' ? 0xba28
                    : 0xbb0f,
        3);
    float_finish(g);
  } else {
    int yes = label(g), done = label(g);
    ins(g, 0x20, 0xbc5b, 3); /* FAC (right) compared with memory (left). */
    ins(g, 0xc9,
        op == T_EQ || op == T_NE  ? 0
        : op == '<' || op == T_GE ? 1
                                  : 255,
        2);
    branch(g, op == T_NE || op == T_LE || op == T_GE ? 0xd0 : 0xf0, yes);
    constant(g, 0);
    jump(g, done);
    bind(g, yes);
    constant(g, 1);
    bind(g, done);
  }
}
static void float_negate(Backend *g) {
  int done = label(g);
  float_copy(g, g->float_result, 0);
  ref(g, 0xad, g->float_result, 0);
  branch(g, 0xf0, done);
  ref(g, 0xad, g->float_result, 1);
  ins(g, 0x49, 128, 2);
  ref(g, 0x8d, g->float_result, 1);
  bind(g, done);
  float_pointer(g, g->float_result, 0);
}

static void emit_float_helpers(Backend *g) {
  emit_float_divmod(g);
  if (g->float_to_integer) {
    int zero = label(g), right = label(g), left_loop = label(g),
        right_loop = label(g), result = label(g), done = label(g), j;
    bind(g, g->float_to_integer);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ins(g, 0xc9, 129, 2);
    branch(g, 0x90, zero);
    ins(g, 0xc9, 176, 2);
    branch(g, 0xb0, zero);
    ref(g, 0x8d, g->float_integer_work, 5);
    for (j = 0; j < 4; ++j) {
      ins(g, 0xa0, 4 - j, 2);
      ins(g, 0xb1, P, 2);
      if (j == 3) {
        ref(g, 0x8d, g->float_integer_work, 4); /* Sign. */
        ins(g, 0x09, 128, 2); /* Implicit high significand bit. */
      }
      ref(g, 0x8d, g->float_integer_work, j);
    }
    ref(g, 0xad, g->float_integer_work, 5);
    ins(g, 0xc9, 160, 2);
    branch(g, 0x90, right);
    ins(g, 0xe9, 160, 2); /* CMP leaves carry set. */
    ins(g, 0xaa, 0, 1);
    branch(g, 0xf0, result);
    bind(g, left_loop);
    ref(g, 0x0e, g->float_integer_work, 0);
    ref(g, 0x2e, g->float_integer_work, 1);
    ins(g, 0xca, 0, 1);
    branch(g, 0xd0, left_loop);
    jump(g, result);
    bind(g, right);
    ins(g, 0xa9, 160, 2);
    ins(g, 0x38, 0, 1);
    ref(g, 0xed, g->float_integer_work, 5);
    ins(g, 0xaa, 0, 1);
    bind(g, right_loop);
    ref(g, 0x4e, g->float_integer_work, 3);
    for (j = 2; j >= 0; --j)
      ref(g, 0x6e, g->float_integer_work, j);
    ins(g, 0xca, 0, 1);
    branch(g, 0xd0, right_loop);
    bind(g, result);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->float_integer_work, j);
      ins(g, 0x85, R + j, 2);
    }
    ref(g, 0xad, g->float_integer_work, 4);
    branch(g, 0x10, done);
    negate_result(g);
    bind(g, done);
    ins(g, 0x60, 0, 1);
    bind(g, zero);
    constant(g, 0);
    ins(g, 0x60, 0, 1);
  }
  if (g->float_parse) {
    int scan = label(g), end = label(g), digit = label(g), sign = label(g),
        dot = label(g), exponent = label(g), space = label(g),
        underscore = label(g), copy = label(g), advance = label(g),
        state2 = label(g), state4 = label(g), state7 = label(g),
        digits_ready = label(g), sign_ready = label(g),
        sign_state_ready = label(g), dot_ready = label(g),
        dot_state_ready = label(g), exponent_ready = label(g),
        space_ready = label(g), end_ready = label(g), white_ready = label(g);
    bind(g, g->float_parse);
    ins(g, 0xa5, R, 2);
    ins(g, 0x85, P, 2);
    ins(g, 0xa5, RH, 2);
    ins(g, 0x85, PH, 2);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->float_state, 146); /* Canonical text length. */
    ref(g, 0x8d, g->float_state, 147); /* Grammar state. */
    ref(g, 0x8d, g->float_state, 149); /* Previous character was a digit. */
    ref(g, 0x8d, g->float_state, 150); /* Underscore requires next digit. */
    bind(g, scan);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ref(g, 0x8d, g->float_state, 148);
    branch(g, 0xf0, end);
    ins(g, 0xc9, '0', 2);
    branch(g, 0x90, digits_ready);
    ins(g, 0xc9, '9' + 1, 2);
    branch(g, 0x90, digit);
    bind(g, digits_ready);
    ref(g, 0xad, g->float_state, 150);
    branch(g, 0xd0, g->trap);
    ref(g, 0xad, g->float_state, 148);
    ins(g, 0xc9, '_', 2);
    branch(g, 0xf0, underscore);
    ins(g, 0xc9, '+', 2);
    branch(g, 0xf0, sign);
    ins(g, 0xc9, '-', 2);
    branch(g, 0xf0, sign);
    ins(g, 0xc9, '.', 2);
    branch(g, 0xf0, dot);
    ins(g, 0xc9, 'e', 2);
    branch(g, 0xf0, exponent);
    ins(g, 0xc9, 'E', 2);
    branch(g, 0xf0, exponent);
    ins(g, 0xc9, 32, 2);
    branch(g, 0xf0, space);
    ins(g, 0xc9, 9, 2);
    branch(g, 0x90, g->trap);
    ins(g, 0xc9, 14, 2);
    branch(g, 0x90, space);
    jump(g, g->trap);
    bind(g, digit);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->float_state, 150);
    ref(g, 0xad, g->float_state, 147);
    ins(g, 0xc9, 8, 2);
    branch(g, 0xf0, g->trap);
    ins(g, 0xc9, 2, 2);
    branch(g, 0x90, state2);
    ins(g, 0xc9, 3, 2);
    branch(g, 0xf0, state4);
    ins(g, 0xc9, 5, 2);
    branch(g, 0xb0, state7);
    jump(g, white_ready);
    bind(g, state2);
    ins(g, 0xa9, 2, 2);
    jump(g, white_ready);
    bind(g, state4);
    ins(g, 0xa9, 4, 2);
    jump(g, white_ready);
    bind(g, state7);
    ins(g, 0xa9, 7, 2);
    bind(g, white_ready);
    ref(g, 0x8d, g->float_state, 147);
    ins(g, 0xa9, 1, 2);
    ref(g, 0x8d, g->float_state, 149);
    jump(g, copy);
    bind(g, underscore);
    ref(g, 0xad, g->float_state, 149);
    branch(g, 0xf0, g->trap);
    ins(g, 0xa9, 1, 2);
    ref(g, 0x8d, g->float_state, 150);
    jump(g, advance);
    bind(g, sign);
    ref(g, 0xad, g->float_state, 147);
    branch(g, 0xf0, sign_ready);
    ins(g, 0xc9, 5, 2);
    branch(g, 0xd0, g->trap);
    ins(g, 0xa9, 6, 2);
    jump(g, sign_state_ready);
    bind(g, sign_ready);
    ins(g, 0xa9, 1, 2);
    bind(g, sign_state_ready);
    ref(g, 0x8d, g->float_state, 147);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->float_state, 149);
    jump(g, copy);
    bind(g, dot);
    ref(g, 0xad, g->float_state, 151);
    branch(g, 0xd0, g->trap);
    ref(g, 0xad, g->float_state, 147);
    ins(g, 0xc9, 2, 2);
    branch(g, 0x90, dot_ready);
    branch(g, 0xd0, g->trap);
    ins(g, 0xa9, 4, 2);
    jump(g, dot_state_ready);
    bind(g, dot_ready);
    ins(g, 0xa9, 3, 2);
    bind(g, dot_state_ready);
    ref(g, 0x8d, g->float_state, 147);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->float_state, 149);
    jump(g, copy);
    bind(g, exponent);
    ref(g, 0xad, g->float_state, 151);
    branch(g, 0xd0, g->trap);
    ref(g, 0xad, g->float_state, 147);
    ins(g, 0xc9, 2, 2);
    branch(g, 0xf0, exponent_ready);
    ins(g, 0xc9, 4, 2);
    branch(g, 0xd0, g->trap);
    bind(g, exponent_ready);
    ins(g, 0xa9, 5, 2);
    ref(g, 0x8d, g->float_state, 147);
    ins(g, 0xa9, 'E', 2);
    ref(g, 0x8d, g->float_state, 148);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->float_state, 149);
    jump(g, copy);
    bind(g, space);
    ref(g, 0xad, g->float_state, 147);
    branch(g, 0xf0, advance);
    ins(g, 0xc9, 2, 2);
    branch(g, 0xf0, space_ready);
    ins(g, 0xc9, 4, 2);
    branch(g, 0xf0, space_ready);
    ins(g, 0xc9, 7, 2);
    branch(g, 0x90, g->trap);
    bind(g, space_ready);
    ins(g, 0xa9, 8, 2);
    ref(g, 0x8d, g->float_state, 147);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->float_state, 149);
    jump(g, advance);
    bind(g, copy);
    ref(g, 0xae, g->float_state, 146);
    ins(g, 0xe0, 255, 2);
    branch(g, 0xf0, g->trap);
    ref(g, 0xad, g->float_state, 148);
    ref(g, 0x9d, g->float_text, 0);
    ref(g, 0xee, g->float_state, 146);
    bind(g, advance);
    ins(g, 0xe6, P, 2);
    branch(g, 0xd0, scan);
    ins(g, 0xe6, PH, 2);
    jump(g, scan);
    bind(g, end);
    ref(g, 0xad, g->float_state, 150);
    branch(g, 0xd0, g->trap);
    ref(g, 0xad, g->float_state, 147);
    ins(g, 0xc9, 2, 2);
    branch(g, 0xf0, end_ready);
    ins(g, 0xc9, 4, 2);
    branch(g, 0xf0, end_ready);
    ins(g, 0xc9, 7, 2);
    branch(g, 0x90, g->trap);
    bind(g, end_ready);
    ins(g, 0xa9, 0, 2);
    ref(g, 0xae, g->float_state, 146);
    ref(g, 0x9d, g->float_text, 0);
    set_pointer(g, g->float_text, 0);
    ins(g, 0x38, 0, 1);
    ins(g, 0xa5, P, 2);
    ins(g, 0xe9, 1, 2);
    ins(g, 0x85, P, 2);
    ins(g, 0xa5, PH, 2);
    ins(g, 0xe9, 0, 2);
    ins(g, 0x85, PH, 2);
    ins(g, 0xa5, P, 2);
    ins(g, 0x85, 0x7a, 2);
    ins(g, 0xa5, PH, 2);
    ins(g, 0x85, 0x7b, 2);
    ins(g, 0xa9, 0, 2);
    ins(g, 0x85, 0x68, 2);
    ins(g, 0x20, 0x0073, 3);
    ins(g, 0x20, 0xbcf3, 3);
    ins(g, 0x60, 0, 1);
  }
  if (g->print_float) {
    int loop = label(g), decimal = label(g), exponent = label(g),
        emit = label(g), done = label(g), return_label = label(g),
        zero = label(g);
    bind(g, g->print_float);
    float_fac(g, TY_FLOAT);
    ins(g, 0x20, 0xbddd, 3);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->float_state, 146);
    ref(g, 0x8d, g->float_state, 147);
    ref(g, 0x8d, g->float_state, 148);
    ins(g, 0xad, 0x0100, 3);
    ins(g, 0xc9, 32, 2);
    branch(g, 0xd0, loop);
    ref(g, 0xee, g->float_state, 148);
    bind(g, loop);
    ref(g, 0xae, g->float_state, 148);
    ins(g, 0xbd, 0x0100, 3);
    ref(g, 0x8d, g->float_state, 149);
    branch(g, 0xf0, done);
    ins(g, 0xc9, '.', 2);
    branch(g, 0xf0, decimal);
    ins(g, 0xc9, 'E', 2);
    branch(g, 0xf0, exponent);
    jump(g, emit);
    bind(g, decimal);
    ref(g, 0xad, g->float_state, 146);
    branch(g, 0xf0, zero);
    ins(g, 0xc9, '-', 2);
    branch(g, 0xd0, exponent);
    bind(g, zero);
    output_character(g, '0');
    bind(g, exponent);
    ins(g, 0xa9, 1, 2);
    ref(g, 0x8d, g->float_state, 147);
    bind(g, emit);
    ref(g, 0xad, g->float_state, 149);
    ref(g, 0x8d, g->float_state, 146);
    ins(g, 0x20, 0xffd2, 3);
    ref(g, 0xee, g->float_state, 148);
    jump(g, loop);
    bind(g, done);
    ref(g, 0xad, g->float_state, 147);
    branch(g, 0xd0, return_label);
    output_character(g, '.');
    output_character(g, '0');
    bind(g, return_label);
    ins(g, 0x60, 0, 1);
  }
}

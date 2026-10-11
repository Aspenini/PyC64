/* Divide the exact 32-bit significands, retaining an integer quotient of up to
   127 bits and an exact remainder before packing. This avoids cancellation in
   a - floor(a / b) * b and lets modulo work when division would overflow.
   Work: right[5], remainder[4], divisor[4], quotient[16], count, exponent,
   opposite_signs, divisor_sign, mode (0=//, 1=%), nonzero, quotient_bit. */
static void emit_float_divmod(Backend *g) {
  int work = g->float_divmod_work, j;
  int zero, small, compare, subtract, append, shifted, remainder, modulo,
      pack_mod, norm_mod, quotient, norm_quotient, shift_quotient, adjust_small,
      pack_quotient, rounded, done;
  if (!g->float_divmod)
    return;
  zero = label(g);
  small = label(g);
  compare = label(g);
  subtract = label(g);
  append = label(g);
  shifted = label(g);
  remainder = label(g);
  modulo = label(g);
  pack_mod = label(g);
  norm_mod = label(g);
  quotient = label(g);
  norm_quotient = label(g);
  shift_quotient = label(g);
  adjust_small = label(g);
  pack_quotient = label(g);
  rounded = label(g);
  done = label(g);
  bind(g, g->float_divmod);
  ref(g, 0x8d, work, 33);
  ref(g, 0xad, work, 0);
  branch(g, 0xf0, g->trap);
  ref(g, 0xad, g->float_left, 0);
  branch(g, 0xf0, zero);
  ins(g, 0xa9, 0, 2);
  for (j = 0; j < 16; ++j)
    ref(g, 0x8d, work, 13 + j);
  ref(g, 0xad, work, 1);
  ins(g, 0x29, 128, 2);
  ref(g, 0x8d, work, 32);
  ref(g, 0x4d, g->float_left, 1);
  ins(g, 0x29, 128, 2);
  ref(g, 0x8d, work, 31);
  ref(g, 0xad, g->float_left, 0);
  ref(g, 0xcd, work, 0);
  branch(g, 0x90, small);
  ins(g, 0x38, 0, 1);
  ref(g, 0xed, work, 0);
  ref(g, 0x8d, work, 29);
  ref(g, 0xad, work, 0);
  ref(g, 0x8d, work, 30);
  for (j = 0; j < 4; ++j) {
    ref(g, 0xad, g->float_left, 4 - j);
    if (j == 3)
      ins(g, 0x09, 128, 2);
    ref(g, 0x8d, work, 5 + j);
    ref(g, 0xad, work, 4 - j);
    if (j == 3)
      ins(g, 0x09, 128, 2);
    ref(g, 0x8d, work, 9 + j);
  }
  bind(g, compare);
  for (j = 3; j >= 0; --j) {
    int equal = label(g);
    ref(g, 0xad, work, 5 + j);
    ref(g, 0xcd, work, 9 + j);
    branch(g, 0xf0, equal);
    branch(g, 0xb0, subtract);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, work, 35);
    jump(g, append);
    bind(g, equal);
  }
  bind(g, subtract);
  ins(g, 0x38, 0, 1);
  for (j = 0; j < 4; ++j) {
    ref(g, 0xad, work, 5 + j);
    ref(g, 0xed, work, 9 + j);
    ref(g, 0x8d, work, 5 + j);
  }
  ins(g, 0xa9, 1, 2);
  ref(g, 0x8d, work, 35);
  bind(g, append);
  ref(g, 0xad, work, 33);
  branch(g, 0xd0, shifted);
  ref(g, 0x4e, work, 35); /* The emitted quotient bit -> carry. */
  for (j = 0; j < 16; ++j)
    ref(g, 0x2e, work, 13 + j);
  branch(g, 0xb0, g->trap);
  ref(g, 0xad, work, 28);
  branch(g, 0x30, g->trap); /* A 2**127 quotient exceeds C64 finite range. */
  bind(g, shifted);
  ref(g, 0xad, work, 29);
  branch(g, 0xf0, remainder);
  ref(g, 0xce, work, 29);
  ref(g, 0x0e, work, 5);
  for (j = 1; j < 4; ++j)
    ref(g, 0x2e, work, 5 + j);
  /* A carried high bit makes the 33-bit remainder >= the 32-bit divisor.
     Subtraction modulo 2**32 then leaves the exact bounded remainder. */
  branch(g, 0xb0, subtract);
  jump(g, compare);
  bind(g, remainder);
  ref(g, 0xad, work, 5);
  for (j = 1; j < 4; ++j)
    ref(g, 0x0d, work, 5 + j);
  ref(g, 0x8d, work, 34);
  ref(g, 0xad, work, 33);
  branch(g, 0xd0, modulo);
  jump(g, quotient);
  bind(g, modulo);
  ref(g, 0xad, work, 34);
  branch(g, 0xf0, zero);
  ref(g, 0xad, work, 31);
  branch(g, 0xf0, pack_mod);
  /* Python's remainder has the divisor's sign. Correct it at the exact
     significand scale before underflow can discard a tiny remainder. */
  ins(g, 0x38, 0, 1);
  for (j = 0; j < 4; ++j) {
    ref(g, 0xad, work, 9 + j);
    ref(g, 0xed, work, 5 + j);
    ref(g, 0x8d, work, 5 + j);
  }
  bind(g, pack_mod);
  bind(g, norm_mod);
  ref(g, 0xad, work, 8);
  branch(g, 0x30, rounded);
  ref(g, 0xce, work, 30);
  branch(g, 0xf0, zero);
  ref(g, 0x0e, work, 5);
  for (j = 1; j < 4; ++j)
    ref(g, 0x2e, work, 5 + j);
  jump(g, norm_mod);
  bind(g, rounded);
  ref(g, 0xad, work, 30);
  ref(g, 0x8d, g->float_result, 0);
  ref(g, 0xad, work, 8);
  ins(g, 0x29, 127, 2);
  ref(g, 0x0d, work, 32);
  ref(g, 0x8d, g->float_result, 1);
  for (j = 2; j < 5; ++j) {
    ref(g, 0xad, work, 9 - j);
    ref(g, 0x8d, g->float_result, j);
  }
  jump(g, done);
  bind(g, small);
  ins(g, 0xa9, 1, 2);
  ref(g, 0x8d, work, 34);
  ref(g, 0xad, work, 33);
  branch(g, 0xf0, quotient);
  ref(g, 0xad, work, 31);
  branch(g, 0xd0, adjust_small);
  for (j = 0; j < 5; ++j) {
    ref(g, 0xad, g->float_left, j);
    ref(g, 0x8d, g->float_result, j);
  }
  jump(g, done);
  bind(g, adjust_small);
  /* Here |a| < |b| and their signs differ, so a + b is the Python remainder.
     The ROM rounds the difference at the target precision. */
  float_pointer(g, g->float_left, 0);
  float_fac(g, TY_FLOAT);
  set_pointer(g, work, 0);
  ins(g, 0xa5, P, 2);
  ins(g, 0xa4, PH, 2);
  ins(g, 0x20, 0xb867, 3);
  float_finish(g);
  jump(g, done);
  bind(g, quotient);
  ref(g, 0xad, work, 31);
  branch(g, 0xf0, norm_quotient);
  ref(g, 0xad, work, 34);
  branch(g, 0xf0, norm_quotient);
  /* floor(-q-r/b) = -(q+1) for a nonzero exact remainder. */
  for (j = 0; j < 16; ++j) {
    ref(g, 0xee, work, 13 + j);
    branch(g, 0xd0, norm_quotient);
  }
  jump(g, g->trap);
  bind(g, norm_quotient);
  ref(g, 0xad, work, 13);
  for (j = 1; j < 16; ++j)
    ref(g, 0x0d, work, 13 + j);
  branch(g, 0xf0, zero);
  ref(g, 0xad, work, 28);
  branch(g, 0x30, g->trap);
  ins(g, 0xa9, 255, 2);
  ref(g, 0x8d, work, 30);
  ref(g, 0x0e, work, 13);
  for (j = 1; j < 16; ++j)
    ref(g, 0x2e, work, 13 + j);
  bind(g, shift_quotient);
  ref(g, 0xad, work, 28);
  branch(g, 0x30, pack_quotient);
  ref(g, 0xce, work, 30);
  ref(g, 0x0e, work, 13);
  for (j = 1; j < 16; ++j)
    ref(g, 0x2e, work, 13 + j);
  jump(g, shift_quotient);
  bind(g, pack_quotient);
  /* Round the retained 32 bits, including carry to a new exponent. */
  {
    int packed = label(g);
    ref(g, 0xad, work, 24); /* The next discarded significand bit. */
    branch(g, 0x10, packed);
    for (j = 25; j <= 28; ++j) {
      ref(g, 0xee, work, j);
      branch(g, 0xd0, packed);
    }
    ref(g, 0xee, work, 30);
    branch(g, 0xf0, g->trap);
    ins(g, 0xa9, 128, 2);
    ref(g, 0x8d, work, 28);
    bind(g, packed);
    ref(g, 0xad, work, 30);
    ref(g, 0x8d, g->float_result, 0);
    ref(g, 0xad, work, 28);
    ins(g, 0x29, 127, 2);
    ref(g, 0x0d, work, 31);
    ref(g, 0x8d, g->float_result, 1);
    for (j = 2; j < 5; ++j) {
      ref(g, 0xad, work, 29 - j);
      ref(g, 0x8d, g->float_result, j);
    }
    jump(g, done);
  }
  bind(g, zero);
  ins(g, 0xa9, 0, 2);
  for (j = 0; j < 5; ++j)
    ref(g, 0x8d, g->float_result, j);
  bind(g, done);
  ins(g, 0x60, 0, 1);
}

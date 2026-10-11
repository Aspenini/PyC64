/* Synchronous REU access. Capture arguments before masking IRQs; restore the
   original interrupt state after the single read of destructive status bits. */
static void reu_call(Backend *g, int builtin, int *args, int count) {
  int j;
  if (builtin == BI_REU_PRESENT) {
    int done = label(g);
    ins(g, 0x08, 0, 1); /* PHP */
    ins(g, 0x78, 0, 1); /* SEI */
    constant(g, 0);
    ins(g, 0xad, 0xdf02, 3);
    ins(g, 0x48, 0, 1);
    ins(g, 0xad, 0xdf03, 3);
    ins(g, 0x48, 0, 1);
    for (j = 0; j < 2; ++j) {
      int low = j ? 0xaa : 0x55, high = j ? 0x55 : 0xaa;
      ins(g, 0xa9, low, 2);
      ins(g, 0x8d, 0xdf02, 3);
      ins(g, 0xa9, high, 2);
      ins(g, 0x8d, 0xdf03, 3);
      ins(g, 0xad, 0xdf02, 3);
      ins(g, 0xc9, low, 2);
      branch(g, 0xd0, done);
      ins(g, 0xad, 0xdf03, 3);
      ins(g, 0xc9, high, 2);
      branch(g, 0xd0, done);
    }
    constant(g, 1);
    bind(g, done);
    ins(g, 0x68, 0, 1);
    ins(g, 0x8d, 0xdf03, 3);
    ins(g, 0x68, 0, 1);
    ins(g, 0x8d, 0xdf02, 3);
    ins(g, 0x28, 0, 1); /* PLP */
    return;
  }
  if (!g->arguments)
    g->arguments = label(g);
  if (!g->reu_flags)
    g->reu_flags = label(g);
  if (count > g->argument_count)
    g->argument_count = count;
  for (j = 0; j < count; ++j) {
    expr(g, args[j]);
    push(g);
  }
  /* Mask IRQs before captured values enter the shared argument workspace. */
  ins(g, 0x08, 0, 1);
  ins(g, 0x78, 0, 1);
  ins(g, 0x68, 0, 1);
  ref(g, 0x8d, g->reu_flags, 0);
  for (j = count - 1; j >= 0; --j) {
    ins(g, 0x68, 0, 1);
    ref(g, 0x8d, g->arguments, j * 2 + 1);
    ins(g, 0x68, 0, 1);
    ref(g, 0x8d, g->arguments, j * 2);
  }
  ref(g, 0xad, g->arguments, 6);
  ref(g, 0x0d, g->arguments, 7);
  branch(g, 0xf0, g->trap); /* Hardware zero means 65536, never a no-op. */
  ref(g, 0xad, g->arguments, 8);
  ins(g, 0xc9, 4, 2);
  branch(g, 0xb0, g->trap);
  ref(g, 0xad, g->arguments, 10);
  ins(g, 0x29, 63, 2);
  branch(g, 0xd0, g->trap);
  ins(g, 0xa9, 0, 2);
  ins(g, 0x8d, 0xdf09, 3); /* Synchronous ownership: disable REU IRQs. */
  ins(g, 0xad, 0xdf00, 3); /* Clear stale end/error/IRQ before the command. */
  for (j = 0; j < 7; ++j) {
    static const int offsets[] = {0, 1, 2, 3, 4, 6, 7};
    ref(g, 0xad, g->arguments, offsets[j]);
    ins(g, 0x8d, 0xdf02 + j, 3);
  }
  ref(g, 0xad, g->arguments, 10);
  ins(g, 0x8d, 0xdf0a, 3);
  ref(g, 0xad, g->arguments, 8);
  ins(g, 0x09, 0x90, 2); /* Immediate execute, no autoload/FF00 trigger. */
  ins(g, 0x8d, 0xdf01, 3);
  ins(g, 0xad, 0xdf00, 3);
  ins(g, 0x85, R, 2);
  ins(g, 0xa9, 0, 2);
  ins(g, 0x85, RH, 2);
  ref(g, 0xad, g->reu_flags, 0);
  ins(g, 0x48, 0, 1);
  ins(g, 0x28, 0, 1);
}

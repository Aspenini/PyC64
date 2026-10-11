#include "pyc64.h"

/* Native 6502 emitter. Expressions return a canonical word in $FD/$FE.
   $FB/$FC is an indirect pointer. The four user zero-page bytes are preserved.
 */
enum { R = 0xfd, RH = 0xfe, P = 0xfb, PH = 0xfc };
typedef struct {
  int offset, label, addend;
} Fixup;
typedef struct {
  Compiler *c;
  int labels[MAX_LABELS], nlabels;
  Fixup fixups[MAX_FIXUPS];
  int nfixups;
  int lhs, math, string_label, trap, exit_label;
  int arguments, argument_count, sprite_masks;
  int division_work, power_work;
  int enum_work;
  int reu_flags;
  int irq_work, irq_hook, irq_stop, irq_install, irq_line, irq_dispatch,
      irq_backup;
  int irq_size, irq_count, in_irq_function;
  int irq_labels[MAX_SYMBOLS + MAX_NODES + 32];
  int irq_sizes[MAX_SYMBOLS + MAX_NODES + 32];
  int irq_offsets[MAX_SYMBOLS + MAX_NODES + 32];
  unsigned char irq_temps[MAX_NODES];
  int file_work; /* READST, KERNAL error, result byte, text pointer. */
  int file_name; /* PETSCII filename scratch for SETNAM, at most 255 bytes. */
  int print_string, print_unsigned, print_signed, print_bool, print_work;
  int characters; /* Immutable one-character strings for string iteration. */
  int string_length, string_compare, string_work;
  int input_line, input_work, input_buffer, input_pool;
  int float_result, float_left, float_state, float_text, float_65536;
  int float_parse, print_float, float_arguments, float_argument_count;
  int float_to_integer, float_integer_work;
  int float_divmod, float_divmod_work;
  int bytes_label, bytes_compare, print_bytes, bytes_work;
  int bytes_construct, bytes_pool, bytes_used;
  int bytes_labels[MAX_BYTES + 1];
  int prefix_data;
  int music_work, music_start, music_stop, music_tick, music_invoke,
      music_silence, music_empty, music_zp, music_foreground, music_cia_prepare,
      music_cia_restore, music_cia_save, music_rsid_prepare, music_rsid_restore,
      music_rsid_invoke, music_rsid_resume;
  int music_end, rsid_used;
  int bytes_allocate, bytes_join, bytes_repeat, bytes_copy;
  int bytes_slice;
  int bytes_search, bytes_match;
  int float_literals[MAX_NODES];
  int string_labels[MAX_STRINGS + 1];
  int breaks[64], continues[64], loops;
  int for_labels[MAX_NODES];
  int list_labels[MAX_NODES];
} Backend;
static int pc(Backend *g) { return 0x0801 + g->c->image_size; }
static int label(Backend *g) {
  int i = ++g->nlabels;
  if (i == MAX_LABELS)
    fail(g->c, 0, "label arena exhausted");
  g->labels[i] = -1;
  return i;
}
static void bind(Backend *g, int l) {
  g->labels[l] = pc(g);
  if (g->c->listing)
    fprintf(g->c->listing, "L%d: ; $%04X\n", l, pc(g));
}
static int byte_literal_label(Backend *g, int offset) {
  if (!g->bytes_label)
    g->bytes_label = label(g);
  if (!g->bytes_labels[offset])
    g->bytes_labels[offset] = offset == 2 ? g->bytes_label : label(g);
  return g->bytes_labels[offset];
}
static int string_literal_label(Backend *g, int offset) {
  if (!g->string_labels[offset])
    g->string_labels[offset] =
        offset == g->c->nstrings ? g->string_label : label(g);
  return g->string_labels[offset];
}
static void byte(Backend *g, int b) {
  if (pc(g) >= 0xa000)
    fail(g->c, 0, "program exceeds BASIC-visible RAM ($0801..$9FFF)");
  g->c->image[g->c->image_size++] = (unsigned char)b;
}
static const char *mnemonic(int op) {
  switch (op) {
  case 0xa9:
  case 0xa5:
  case 0xb5:
  case 0xad:
  case 0xbd:
  case 0xb1:
    return "lda";
  case 0x85:
  case 0x95:
  case 0x8d:
  case 0x9d:
  case 0x99:
  case 0x91:
    return "sta";
  case 0xa2:
  case 0xa6:
  case 0xae:
    return "ldx";
  case 0xa0:
  case 0xa4:
  case 0xac:
    return "ldy";
  case 0x48:
    return "pha";
  case 0x68:
    return "pla";
  case 0x08:
    return "php";
  case 0x28:
    return "plp";
  case 0x78:
    return "sei";
  case 0x58:
    return "cli";
  case 0x40:
    return "rti";
  case 0x18:
    return "clc";
  case 0x38:
    return "sec";
  case 0xd8:
    return "cld";
  case 0x65:
  case 0x6d:
  case 0x69:
    return "adc";
  case 0xe5:
  case 0xed:
  case 0xe9:
    return "sbc";
  case 0x25:
  case 0x2d:
  case 0x29:
    return "and";
  case 0x05:
  case 0x0d:
  case 0x09:
    return "ora";
  case 0x45:
  case 0x4d:
  case 0x49:
    return "eor";
  case 0xc5:
  case 0xcd:
  case 0xc9:
    return "cmp";
  case 0xe0:
    return "cpx";
  case 0xe6:
  case 0xee:
    return "inc";
  case 0xc6:
  case 0xce:
    return "dec";
  case 0x06:
  case 0x0a:
  case 0x0e:
    return "asl";
  case 0x26:
  case 0x2e:
    return "rol";
  case 0x46:
  case 0x4a:
  case 0x4e:
    return "lsr";
  case 0x66:
  case 0x6e:
    return "ror";
  case 0x4c:
  case 0x6c:
    return "jmp";
  case 0x20:
    return "jsr";
  case 0x60:
    return "rts";
  case 0xd0:
    return "bne";
  case 0xf0:
    return "beq";
  case 0x90:
    return "bcc";
  case 0xb0:
    return "bcs";
  case 0x10:
    return "bpl";
  case 0x30:
    return "bmi";
  case 0x50:
    return "bvc";
  case 0x70:
    return "bvs";
  case 0xca:
    return "dex";
  case 0xe8:
    return "inx";
  case 0xc8:
    return "iny";
  case 0xaa:
    return "tax";
  case 0x8a:
    return "txa";
  case 0x98:
    return "tya";
  case 0x00:
    return "brk";
  default:
    return ".byte";
  }
}
static void ins(Backend *g, int op, int operand, int size) {
  FILE *f = g->c->listing;
  if (f) {
    fprintf(f, "    %s", mnemonic(op));
    if (size == 2)
      fprintf(f, " %s$%02X",
              op == 0xa9 || op == 0xa2 || op == 0xa0 || op == 0x69 ||
                      op == 0xe9 || op == 0x29 || op == 0x09 || op == 0x49 ||
                      op == 0xc9
                  ? "#"
                  : "",
              operand & 255);
    if (size == 3)
      fprintf(f, " $%04X", operand & 65535);
    if (op == 0xb1 || op == 0x91)
      fprintf(f, " ; indirect,Y");
    fprintf(f, " ; $%04X\n", pc(g));
  }
  byte(g, op);
  if (size > 1)
    byte(g, operand);
  if (size > 2)
    byte(g, operand >> 8);
}
static void ref(Backend *g, int op, int l, int add) {
  Fixup *f;
  if (g->nfixups == MAX_FIXUPS)
    fail(g->c, 0, "relocation arena exhausted");
  if (g->c->listing)
    fprintf(g->c->listing, "    %s L%d+%d ; $%04X\n", mnemonic(op), l, add,
            pc(g));
  byte(g, op);
  f = &g->fixups[g->nfixups++];
  f->offset = g->c->image_size;
  f->label = l;
  f->addend = add;
  byte(g, 0);
  byte(g, 0);
}
static void jump(Backend *g, int l) { ref(g, 0x4c, l, 0); }
/* Every conditional jump uses a short inverted branch over an absolute JMP. */
static void branch(Backend *g, int op, int l) {
  ins(g, op ^ 0x20, 3, 2);
  jump(g, l);
}
static void constant(Backend *g, long v) {
  ins(g, 0xa9, (int)v & 255, 2);
  ins(g, 0x85, R, 2);
  ins(g, 0xa9, (int)(v & 65535) >> 8, 2);
  ins(g, 0x85, RH, 2);
}
static void normalize(Backend *g, int type) {
  type = integer_type(type);
  if (type == TY_U8 || type == TY_BOOL) {
    ins(g, 0xa9, 0, 2);
    ins(g, 0x85, RH, 2);
  }
  if (type == TY_I8) {
    int done = label(g);
    ins(g, 0xa9, 0, 2);
    ins(g, 0x85, RH, 2);
    ins(g, 0xa5, R, 2);
    branch(g, 0x10, done);
    ins(g, 0xa9, 255, 2);
    ins(g, 0x85, RH, 2);
    bind(g, done);
  }
}
static void truth(Backend *g, int type) {
  if (type == TY_BYTES) {
    ins(g, 0x38, 0, 1);
    ins(g, 0xa5, R, 2);
    ins(g, 0xe9, 2, 2);
    ins(g, 0x85, P, 2);
    ins(g, 0xa5, RH, 2);
    ins(g, 0xe9, 0, 2);
    ins(g, 0x85, PH, 2);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ins(g, 0xc8, 0, 1);
    ins(g, 0x11, P, 2);
    return;
  }
  if (type == TY_STR || type == TY_FLOAT) {
    ins(g, 0xa5, R, 2);
    ins(g, 0x85, P, 2);
    ins(g, 0xa5, RH, 2);
    ins(g, 0x85, PH, 2);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
  } else {
    ins(g, 0xa5, R, 2);
    ins(g, 0x05, RH, 2);
  }
}
static void boolean(Backend *g, int type) {
  int no = label(g), done = label(g);
  truth(g, type);
  branch(g, 0xf0, no);
  constant(g, 1);
  jump(g, done);
  bind(g, no);
  constant(g, 0);
  bind(g, done);
}
static void push(Backend *g) {
  ins(g, 0xa5, R, 2);
  ins(g, 0x48, 0, 1);
  ins(g, 0xa5, RH, 2);
  ins(g, 0x48, 0, 1);
}
static void pop_lhs(Backend *g) {
  ins(g, 0x68, 0, 1);
  ref(g, 0x8d, g->lhs, 1);
  ins(g, 0x68, 0, 1);
  ref(g, 0x8d, g->lhs, 0);
}
static void expr(Backend *g, int i);
static int snapshot_list(Backend *g, int i);
static void bytes_binary(Backend *g, Node *n);
static void bytes_constructor(Backend *g, Node *n);
static void bytes_slice_expression(Backend *g, Node *n);
static void bytes_search_expression(Backend *g, Node *n);
static void bytes_membership_expression(Backend *g, Node *n);
static void emit_bytes_search(Backend *g);
static void irq_call(Backend *g, int builtin, Node *n, int *args);
static void music_call(Backend *g, int builtin, int *args);
static void emit_irq_helpers(Backend *g);
static int live_symbol(Compiler *c, Symbol *s);
static void emit_bytes_helpers(Backend *g);
static void negate_result(Backend *g);
static void set_pointer(Backend *g, int l, int add);
static void float_push(Backend *g);
static void float_pop(Backend *g, int destination, int offset);
static void float_copy(Backend *g, int destination, int offset);
static void field_address(Backend *g, Symbol *field) {
  Symbol *object = &g->c->symbols[field->parent - 1];
  if (!object->reference) {
    set_pointer(g, object->label, (int)field->value);
    return;
  }
  ref(g, 0xad, object->label, 0);
  ins(g, 0x18, 0, 1);
  ins(g, 0x69, (int)field->value, 2);
  ins(g, 0x85, P, 2);
  ref(g, 0xad, object->label, 1);
  ins(g, 0x69, 0, 2);
  ins(g, 0x85, PH, 2);
}
static void load_symbol(Backend *g, int s) {
  Symbol *v = &g->c->symbols[s];
  if (v->type == TY_FLOAT) {
    if (v->parent)
      field_address(g, v);
    else
      set_pointer(g, v->label, 0);
    ins(g, 0xa5, P, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa5, PH, 2);
    ins(g, 0x85, RH, 2);
    return;
  }
  if (v->parent) {
    field_address(g, v);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ins(g, 0x85, R, 2);
    if (type_size(v->type) == 2) {
      ins(g, 0xc8, 0, 1);
      ins(g, 0xb1, P, 2);
      ins(g, 0x85, RH, 2);
    } else
      normalize(g, v->type);
    return;
  }
  if (object_type(v->type) && !v->reference) {
    set_pointer(g, v->label, 0);
    ins(g, 0xa5, P, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa5, PH, 2);
    ins(g, 0x85, RH, 2);
    return;
  }
  ref(g, 0xad, v->label, 0);
  ins(g, 0x85, R, 2);
  if (type_size(v->type) == 2) {
    ref(g, 0xad, v->label, 1);
    ins(g, 0x85, RH, 2);
  } else
    normalize(g, v->type);
}
static void store_symbol(Backend *g, int s) {
  Symbol *v = &g->c->symbols[s];
  if (v->type == TY_FLOAT) {
    int j;
    float_push(g);
    if (v->parent)
      field_address(g, v);
    else
      set_pointer(g, v->label, 0);
    for (j = 4; j >= 0; --j) {
      ins(g, 0x68, 0, 1);
      ins(g, 0xa0, j, 2);
      ins(g, 0x91, P, 2);
    }
    return;
  }
  if (v->parent) {
    field_address(g, v);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xa5, R, 2);
    ins(g, 0x91, P, 2);
    if (type_size(v->type) == 2) {
      ins(g, 0xc8, 0, 1);
      ins(g, 0xa5, RH, 2);
      ins(g, 0x91, P, 2);
    }
    return;
  }
  ins(g, 0xa5, R, 2);
  ref(g, 0x8d, v->label, 0);
  if (type_size(v->type) == 2) {
    ins(g, 0xa5, RH, 2);
    ref(g, 0x8d, v->label, 1);
  }
}
static void set_pointer(Backend *g, int l, int add) {
  /* LDA immediate low/high operands are patched after static layout. */
  Fixup *f;
  int k;
  for (k = 0; k < 2; ++k) {
    if (g->nfixups == MAX_FIXUPS)
      fail(g->c, 0, "relocation arena exhausted");
    if (g->c->listing)
      fprintf(g->c->listing, "    lda #%s(L%d+%d)\n", k ? ">" : "<", l, add);
    byte(g, 0xa9);
    f = &g->fixups[g->nfixups++];
    f->offset = g->c->image_size;
    f->label = -l;
    f->addend = add | (k ? 0x10000 : 0);
    byte(g, 0);
    ins(g, 0x85, P + k, 2);
  }
}
static void output_character(Backend *g, int ch);
#include "float_codegen.inc"

static int string_workspace(Backend *g) {
  if (!g->string_work)
    g->string_work = label(g);
  return g->string_work;
}
static int bytes_workspace(Backend *g) {
  if (!g->bytes_work)
    g->bytes_work = label(g);
  return g->bytes_work;
}
static void length_call(Backend *g) {
  if (!g->string_length)
    g->string_length = label(g);
  ref(g, 0x20, g->string_length, 0);
}
/* A contains an ASCII byte. Return its immutable one-character string. */
static void character(Backend *g) {
  if (!g->characters)
    g->characters = label(g);
  ins(g, 0x0a, 0, 1);
  ins(g, 0x0a, 0, 1);
  ins(g, 0x85, R, 2);
  ins(g, 0xa9, 0, 2);
  ins(g, 0x69, 0, 2); /* High bit of character * 4. */
  ins(g, 0x85, RH, 2);
  set_pointer(g, g->characters, 2);
  ins(g, 0x18, 0, 1);
  ins(g, 0xa5, P, 2);
  ins(g, 0x65, R, 2);
  ins(g, 0x85, R, 2);
  ins(g, 0xa5, PH, 2);
  ins(g, 0x65, RH, 2);
  ins(g, 0x85, RH, 2);
}
static void string_index(Backend *g, Node *n) {
  int work = string_workspace(g), positive = label(g), valid = label(g), k;
  expr(g, n->a);
  push(g); /* Preserve the source across an index expression with side effects.
            */
  expr(g, n->b);
  ins(g, 0xa5, R, 2);
  ref(g, 0x8d, work, 4);
  ins(g, 0xa5, RH, 2);
  ref(g, 0x8d, work, 5);
  pop_lhs(g);
  for (k = 0; k < 2; ++k) {
    ref(g, 0xad, g->lhs, k);
    ref(g, 0x8d, work, k);
    ins(g, 0x85, P + k, 2);
  }
  length_call(g);
  ins(g, 0xa5, R, 2);
  ref(g, 0x8d, work, 2);
  ins(g, 0xa5, RH, 2);
  ref(g, 0x8d, work, 3);
  ref(g, 0xad, work, 4);
  ins(g, 0x85, R, 2);
  ref(g, 0xad, work, 5);
  ins(g, 0x85, RH, 2);
  if (type_signed(g->c->nodes[n->b].type)) {
    branch(g, 0x10, positive);
    ins(g, 0x18, 0, 1);
    ins(g, 0xa5, R, 2);
    ref(g, 0x6d, work, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa5, RH, 2);
    ref(g, 0x6d, work, 3);
    ins(g, 0x85, RH, 2);
  }
  bind(g, positive);
  ins(g, 0xa5, RH, 2);
  ref(g, 0xcd, work, 3);
  branch(g, 0x90, valid);
  branch(g, 0xd0, g->trap);
  ins(g, 0xa5, R, 2);
  ref(g, 0xcd, work, 2);
  branch(g, 0xb0, g->trap);
  bind(g, valid);
  ins(g, 0x18, 0, 1);
  ref(g, 0xad, work, 0);
  ins(g, 0x65, R, 2);
  ins(g, 0x85, P, 2);
  ref(g, 0xad, work, 1);
  ins(g, 0x65, RH, 2);
  ins(g, 0x85, PH, 2);
  ins(g, 0xa0, 0, 2);
  ins(g, 0xb1, P, 2);
  if (g->c->nodes[n->a].type == TY_BYTES) {
    ins(g, 0x85, R, 2);
    normalize(g, TY_U8);
  } else
    character(g);
}
static void address(Backend *g, int i) {
  Node *n = &g->c->nodes[i];
  Symbol *s = &g->c->symbols[n->symbol];
  int valid = label(g);
  expr(g, n->b);
  if (s->type == TY_LIST && type_signed(g->c->nodes[n->b].type)) {
    int positive = label(g);
    ins(g, 0xa5, RH, 2);
    branch(g, 0x10, positive);
    ins(g, 0x18, 0, 1);
    ins(g, 0xa5, R, 2);
    if (s->reference)
      ref(g, 0x6d, s->label, 2);
    else
      ins(g, 0x69, s->count & 255, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa5, RH, 2);
    if (s->reference)
      ref(g, 0x6d, s->label, 3);
    else
      ins(g, 0x69, s->count >> 8, 2);
    ins(g, 0x85, RH, 2);
    bind(g, positive);
  }
  ins(g, 0xa5, RH, 2);
  if (s->reference)
    ref(g, 0xcd, s->label, 3);
  else
    ins(g, 0xc9, s->count >> 8, 2);
  branch(g, 0x90, valid);
  branch(g, 0xd0, g->trap);
  ins(g, 0xa5, R, 2);
  if (s->reference)
    ref(g, 0xcd, s->label, 2);
  else
    ins(g, 0xc9, s->count & 255, 2);
  branch(g, 0xb0, g->trap);
  bind(g, valid);
  if (type_size(s->element) == 2) {
    ins(g, 0x06, R, 2);
    ins(g, 0x26, RH, 2);
  }
  if (s->element == TY_FLOAT) {
    ins(g, 0xa5, R, 2);
    ref(g, 0x8d, g->math, 0);
    ins(g, 0xa5, RH, 2);
    ref(g, 0x8d, g->math, 1);
    ins(g, 0x06, R, 2);
    ins(g, 0x26, RH, 2);
    ins(g, 0x06, R, 2);
    ins(g, 0x26, RH, 2);
    ins(g, 0x18, 0, 1);
    ins(g, 0xa5, R, 2);
    ref(g, 0x6d, g->math, 0);
    ins(g, 0x85, R, 2);
    ins(g, 0xa5, RH, 2);
    ref(g, 0x6d, g->math, 1);
    ins(g, 0x85, RH, 2);
  }
  if (s->reference) {
    ref(g, 0xad, s->label, 0);
    ins(g, 0x85, P, 2);
    ref(g, 0xad, s->label, 1);
    ins(g, 0x85, PH, 2);
  } else
    set_pointer(g, s->label, 0);
  ins(g, 0x18, 0, 1);
  ins(g, 0xa5, P, 2);
  ins(g, 0x65, R, 2);
  ins(g, 0x85, P, 2);
  ins(g, 0xa5, PH, 2);
  ins(g, 0x65, RH, 2);
  ins(g, 0x85, PH, 2);
}
static void compare(Backend *g, int op, int sign) {
  int yes = label(g), no = label(g), done = label(g), low = label(g);
  if (op == T_EQ || op == T_NE) {
    ref(g, 0xad, g->lhs, 1);
    ins(g, 0xc5, RH, 2);
    branch(g, 0xd0, op == T_NE ? yes : no);
    ref(g, 0xad, g->lhs, 0);
    ins(g, 0xc5, R, 2);
    branch(g, op == T_EQ ? 0xf0 : 0xd0, yes);
    jump(g, no);
  } else {
    if (sign) {
      ins(g, 0xa5, RH, 2);
      ins(g, 0x49, 128, 2);
      ref(g, 0x8d, g->math, 0);
      ref(g, 0xad, g->lhs, 1);
      ins(g, 0x49, 128, 2);
      ref(g, 0xcd, g->math, 0);
    } else {
      ref(g, 0xad, g->lhs, 1);
      ins(g, 0xc5, RH, 2);
    }
    branch(g, 0xf0, low);
    branch(g, 0x90, (op == '<' || op == T_LE) ? yes : no);
    jump(g, (op == '>' || op == T_GE) ? yes : no);
    bind(g, low);
    ref(g, 0xad, g->lhs, 0);
    ins(g, 0xc5, R, 2);
    if (op == '<' || op == T_LE) {
      branch(g, 0x90, yes);
      if (op == T_LE)
        branch(g, 0xf0, yes);
      jump(g, no);
    } else {
      branch(g, 0x90, no);
      if (op == '>')
        branch(g, 0xf0, no);
      jump(g, yes);
    }
  }
  bind(g, yes);
  constant(g, 1);
  jump(g, done);
  bind(g, no);
  constant(g, 0);
  bind(g, done);
}
static void multiply(Backend *g) {
  int loop = label(g), skip = label(g), end = label(g);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->math, 0);
  ref(g, 0x8d, g->math, 1);
  bind(g, loop);
  ins(g, 0xa5, R, 2);
  ins(g, 0x05, RH, 2);
  branch(g, 0xf0, end);
  ins(g, 0xa5, R, 2);
  ins(g, 0x29, 1, 2);
  branch(g, 0xf0, skip);
  ins(g, 0x18, 0, 1);
  ref(g, 0xad, g->math, 0);
  ref(g, 0x6d, g->lhs, 0);
  ref(g, 0x8d, g->math, 0);
  ref(g, 0xad, g->math, 1);
  ref(g, 0x6d, g->lhs, 1);
  ref(g, 0x8d, g->math, 1);
  bind(g, skip);
  ref(g, 0x0e, g->lhs, 0);
  ref(g, 0x2e, g->lhs, 1);
  ins(g, 0x46, RH, 2);
  ins(g, 0x66, R, 2);
  jump(g, loop);
  bind(g, end);
  ref(g, 0xad, g->math, 0);
  ins(g, 0x85, R, 2);
  ref(g, 0xad, g->math, 1);
  ins(g, 0x85, RH, 2);
}
static void integer_power(Backend *g) {
  int j, loop = label(g), square = label(g), done = label(g);
  if (!g->power_work)
    g->power_work = label(g);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->lhs, j);
    ref(g, 0x8d, g->power_work, j);
    ins(g, 0xa5, R + j, 2);
    ref(g, 0x8d, g->power_work, 4 + j);
    ins(g, 0xa9, j == 0 ? 1 : 0, 2);
    ref(g, 0x8d, g->power_work, 2 + j);
  }
  bind(g, loop);
  ref(g, 0xad, g->power_work, 4);
  ref(g, 0x0d, g->power_work, 5);
  branch(g, 0xf0, done);
  ref(g, 0xad, g->power_work, 4);
  ins(g, 0x29, 1, 2);
  branch(g, 0xf0, square);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->power_work, j);
    ref(g, 0x8d, g->lhs, j);
    ref(g, 0xad, g->power_work, 2 + j);
    ins(g, 0x85, R + j, 2);
  }
  multiply(g);
  for (j = 0; j < 2; ++j) {
    ins(g, 0xa5, R + j, 2);
    ref(g, 0x8d, g->power_work, 2 + j);
  }
  bind(g, square);
  ref(g, 0x4e, g->power_work, 5);
  ref(g, 0x6e, g->power_work, 4);
  ref(g, 0xad, g->power_work, 4);
  ref(g, 0x0d, g->power_work, 5);
  branch(g, 0xf0, done);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->power_work, j);
    ref(g, 0x8d, g->lhs, j);
    ins(g, 0x85, R + j, 2);
  }
  multiply(g);
  for (j = 0; j < 2; ++j) {
    ins(g, 0xa5, R + j, 2);
    ref(g, 0x8d, g->power_work, j);
  }
  jump(g, loop);
  bind(g, done);
  for (j = 0; j < 2; ++j) {
    ref(g, 0xad, g->power_work, 2 + j);
    ins(g, 0x85, R + j, 2);
  }
}
static void divide(Backend *g, int remainder) {
  int loop = label(g), subtract = label(g), skip = label(g);
  ins(g, 0xa5, R, 2);
  ins(g, 0x05, RH, 2);
  branch(g, 0xf0, g->trap);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->math, 0);
  ref(g, 0x8d, g->math, 1);
  ins(g, 0xa2, 16, 2);
  bind(g, loop);
  ref(g, 0x0e, g->lhs, 0);
  ref(g, 0x2e, g->lhs, 1);
  ref(g, 0x2e, g->math, 0);
  ref(g, 0x2e, g->math, 1);
  branch(g, 0xb0, subtract);
  ref(g, 0xad, g->math, 1);
  ins(g, 0xc5, RH, 2);
  branch(g, 0x90, skip);
  branch(g, 0xd0, subtract);
  ref(g, 0xad, g->math, 0);
  ins(g, 0xc5, R, 2);
  branch(g, 0x90, skip);
  bind(g, subtract);
  ins(g, 0x38, 0, 1);
  ref(g, 0xad, g->math, 0);
  ins(g, 0xe5, R, 2);
  ref(g, 0x8d, g->math, 0);
  ref(g, 0xad, g->math, 1);
  ins(g, 0xe5, RH, 2);
  ref(g, 0x8d, g->math, 1);
  ref(g, 0xee, g->lhs, 0);
  bind(g, skip);
  ins(g, 0xca, 0, 1);
  branch(g, 0xd0, loop);
  ref(g, 0xad, remainder ? g->math : g->lhs, 0);
  ins(g, 0x85, R, 2);
  ref(g, 0xad, remainder ? g->math : g->lhs, 1);
  ins(g, 0x85, RH, 2);
}
static void negate_result(Backend *g) {
  ins(g, 0x38, 0, 1);
  ins(g, 0xa9, 0, 2);
  ins(g, 0xe5, R, 2);
  ins(g, 0x85, R, 2);
  ins(g, 0xa9, 0, 2);
  ins(g, 0xe5, RH, 2);
  ins(g, 0x85, RH, 2);
}
static void signed_divide(Backend *g, int remainder) {
  int left_positive = label(g), right_positive = label(g), done = label(g);
  int same_sign = label(g), apply_sign = label(g);
  if (!g->division_work)
    g->division_work = label(g);
  /* Absolute magnitudes allow -32768 to reuse unsigned 16-bit division. */
  ref(g, 0xad, g->lhs, 1);
  ins(g, 0x29, 128, 2);
  ref(g, 0x8d, g->division_work, 0);
  branch(g, 0xf0, left_positive);
  ins(g, 0x38, 0, 1);
  ins(g, 0xa9, 0, 2);
  ref(g, 0xed, g->lhs, 0);
  ref(g, 0x8d, g->lhs, 0);
  ins(g, 0xa9, 0, 2);
  ref(g, 0xed, g->lhs, 1);
  ref(g, 0x8d, g->lhs, 1);
  bind(g, left_positive);
  ins(g, 0xa5, RH, 2);
  ins(g, 0x29, 128, 2);
  ref(g, 0x8d, g->division_work, 1);
  branch(g, 0xf0, right_positive);
  negate_result(g);
  bind(g, right_positive);
  ins(g, 0xa5, R, 2);
  ref(g, 0x8d, g->division_work, 2);
  ins(g, 0xa5, RH, 2);
  ref(g, 0x8d, g->division_work, 3);
  divide(g, remainder);
  if (remainder) {
    ins(g, 0xa5, R, 2);
    ins(g, 0x05, RH, 2);
    branch(g, 0xf0, done);
  }
  ref(g, 0xad, g->division_work, 0);
  ref(g, 0x4d, g->division_work, 1);
  branch(g, 0xf0, same_sign);
  if (remainder) {
    /* Opposite signs: Python's remainder is |divisor| - remainder. */
    ins(g, 0x38, 0, 1);
    ref(g, 0xad, g->division_work, 2);
    ins(g, 0xe5, R, 2);
    ins(g, 0x85, R, 2);
    ref(g, 0xad, g->division_work, 3);
    ins(g, 0xe5, RH, 2);
    ins(g, 0x85, RH, 2);
    bind(g, same_sign);
    ref(g, 0xad, g->division_work, 1);
    branch(g, 0xf0, done);
    negate_result(g);
  } else {
    negate_result(g);
    ref(g, 0xad, g->math, 0);
    ref(g, 0x0d, g->math, 1);
    branch(g, 0xf0, apply_sign);
    /* Truncation is one too high when a negative quotient has a remainder. */
    ins(g, 0x38, 0, 1);
    ins(g, 0xa5, R, 2);
    ins(g, 0xe9, 1, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa5, RH, 2);
    ins(g, 0xe9, 0, 2);
    ins(g, 0x85, RH, 2);
    bind(g, apply_sign);
    bind(g, same_sign);
  }
  bind(g, done);
}
static void binary(Backend *g, Node *n) {
  if (n->op == OP_IN || n->op == OP_NOTIN) {
    bytes_membership_expression(g, n);
    return;
  }
  if (n->d == 2) {
    expr(g, n->a);
    expr(g, n->b);
    constant(g, n->op == T_NE);
    return;
  }
  int op = n->op;
  if (op == OP_AND || op == OP_OR) {
    int done = label(g);
    expr(g, n->a);
    truth(g, g->c->nodes[n->a].type);
    branch(g, op == OP_AND ? 0xf0 : 0xd0, done);
    expr(g, n->b);
    bind(g, done);
    return;
  }
  if (g->c->nodes[n->a].type == TY_FLOAT) {
    float_binary(g, n);
    return;
  }
  expr(g, n->a);
  push(g);
  expr(g, n->b);
  pop_lhs(g);
  if (n->type == TY_BYTES && (op == '+' || op == '*')) {
    bytes_binary(g, n);
    return;
  }
  if (g->c->nodes[n->a].type == TY_BYTES) {
    int work = bytes_workspace(g), j;
    if (!g->bytes_compare)
      g->bytes_compare = label(g);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->lhs, j);
      ref(g, 0x8d, work, j);
      ins(g, 0xa5, R + j, 2);
      ref(g, 0x8d, work, j + 2);
    }
    ref(g, 0x20, g->bytes_compare, 0);
    for (j = 0; j < 2; ++j) {
      ins(g, 0xa5, R + j, 2);
      ref(g, 0x8d, g->lhs, j);
    }
    constant(g, 0);
    compare(g, op, 1);
    return;
  }
  if (g->c->nodes[n->a].type == TY_STR) {
    int work = string_workspace(g), j;
    if (!g->string_compare)
      g->string_compare = label(g);
    for (j = 0; j < 2; ++j) {
      ref(g, 0xad, g->lhs, j);
      ref(g, 0x8d, work, j);
      ins(g, 0xa5, R + j, 2);
      ref(g, 0x8d, work, j + 2);
    }
    ref(g, 0x20, g->string_compare, 0);
    ins(g, 0xa5, R, 2);
    ref(g, 0x8d, g->lhs, 0);
    ins(g, 0xa5, RH, 2);
    ref(g, 0x8d, g->lhs, 1);
    constant(g, 0);
    compare(g, op, 1);
    return;
  }
  if (op == '+' || op == '-') {
    ins(g, op == '+' ? 0x18 : 0x38, 0, 1);
    ref(g, 0xad, g->lhs, 0);
    ins(g, op == '+' ? 0x65 : 0xe5, R, 2);
    ins(g, 0x85, R, 2);
    ref(g, 0xad, g->lhs, 1);
    ins(g, op == '+' ? 0x65 : 0xe5, RH, 2);
    ins(g, 0x85, RH, 2);
  } else if (op == '&' || op == '|' || op == '^') {
    int k = op == '&' ? 0x25 : op == '|' ? 0x05 : 0x45;
    ref(g, 0xad, g->lhs, 0);
    ins(g, k, R, 2);
    ins(g, 0x85, R, 2);
    ref(g, 0xad, g->lhs, 1);
    ins(g, k, RH, 2);
    ins(g, 0x85, RH, 2);
  } else if (op == '*')
    multiply(g);
  else if (op == T_POWER)
    integer_power(g);
  else if (op == T_FLOORDIV || op == '%') {
    if (type_signed(n->type))
      signed_divide(g, op == '%');
    else
      divide(g, op == '%');
  } else if (op == T_SHL || op == T_SHR) {
    int loop = label(g), done = label(g);
    ins(g, 0xa5, RH, 2);
    branch(g, 0xd0, g->trap);
    ins(g, 0xa5, R, 2);
    ins(g, 0xc9, 16, 2);
    branch(g, 0xb0, g->trap);
    ins(g, 0xaa, 0, 1);
    bind(g, loop);
    ins(g, 0x8a, 0, 1);
    branch(g, 0xf0, done);
    if (op == T_SHL) {
      ref(g, 0x0e, g->lhs, 0);
      ref(g, 0x2e, g->lhs, 1);
    } else {
      if (type_signed(n->type)) {
        ref(g, 0xad, g->lhs, 1);
        ins(g, 0xc9, 128, 2);
        ref(g, 0x6e, g->lhs, 1);
      } else
        ref(g, 0x4e, g->lhs, 1);
      ref(g, 0x6e, g->lhs, 0);
    }
    ins(g, 0xca, 0, 1);
    jump(g, loop);
    bind(g, done);
    ref(g, 0xad, g->lhs, 0);
    ins(g, 0x85, R, 2);
    ref(g, 0xad, g->lhs, 1);
    ins(g, 0x85, RH, 2);
  } else
    compare(g, op, n->d);
  normalize(g, n->type);
}
static int petscii(int ch) {
  return ch == 10 ? 13 : ch >= 'a' && ch <= 'z' ? ch - 32 : ch;
}
static int screen_code(int ch) {
  ch = petscii(ch);
  return ch >= 64 && ch <= 95 ? ch - 64 : ch;
}
/* Strings retain ASCII bytes internally. C64 text boundaries use PETSCII. */
static void to_petscii(Backend *g) {
  int done = label(g), letters = label(g);
  ins(g, 0xc9, 10, 2);
  branch(g, 0xd0, letters);
  ins(g, 0xa9, 13, 2);
  jump(g, done);
  bind(g, letters);
  ins(g, 0xc9, 'a', 2);
  branch(g, 0x90, done);
  ins(g, 0xc9, 'z' + 1, 2);
  branch(g, 0xb0, done);
  ins(g, 0x38, 0, 1);
  ins(g, 0xe9, 32, 2);
  bind(g, done);
}
static void output_character(Backend *g, int ch) {
  ins(g, 0xa9, ch, 2);
  ins(g, 0x20, 0xffd2, 3);
}
static void output_text(Backend *g, const char *text) {
  for (; *text; ++text)
    output_character(g, petscii((unsigned char)*text));
}
static void save_arguments(Backend *g, int *args, int count) {
  int j;
  if (!count)
    return;
  if (!g->arguments)
    g->arguments = label(g);
  if (count > g->argument_count)
    g->argument_count = count;
  /* Evaluate every argument before output, preserving values across nested
   * calls. */
  for (j = 0; j < count; ++j) {
    expr(g, args[j]);
    push(g);
  }
  for (j = count - 1; j >= 0; --j) {
    ins(g, 0x68, 0, 1);
    ref(g, 0x8d, g->arguments, j * 2 + 1);
    ins(g, 0x68, 0, 1);
    ref(g, 0x8d, g->arguments, j * 2);
  }
}
static void emit_print_helpers(Backend *g) {
  int j;
  if (g->print_string) {
    int loop = label(g), done = label(g);
    bind(g, g->print_string);
    ins(g, 0xa5, R, 2);
    ins(g, 0x85, P, 2);
    ins(g, 0xa5, RH, 2);
    ins(g, 0x85, PH, 2);
    bind(g, loop);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    branch(g, 0xf0, done);
    to_petscii(g);
    ins(g, 0x20, 0xffd2, 3);
    ins(g, 0xe6, P, 2);
    branch(g, 0xd0, loop);
    ins(g, 0xe6, PH, 2);
    jump(g, loop);
    bind(g, done);
    ins(g, 0x60, 0, 1);
  }
  if (g->print_bool) {
    int false_value = label(g);
    bind(g, g->print_bool);
    ins(g, 0xa5, R, 2);
    ins(g, 0x05, RH, 2);
    branch(g, 0xf0, false_value);
    output_text(g, "True");
    ins(g, 0x60, 0, 1);
    bind(g, false_value);
    output_text(g, "False");
    ins(g, 0x60, 0, 1);
  }
  if (g->print_signed) {
    int positive = label(g);
    bind(g, g->print_signed);
    ins(g, 0xa5, RH, 2);
    branch(g, 0x10, positive);
    ins(g, 0x38, 0, 1);
    ins(g, 0xa9, 0, 2);
    ins(g, 0xe5, R, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa9, 0, 2);
    ins(g, 0xe5, RH, 2);
    ins(g, 0x85, RH, 2);
    output_character(g, '-');
    bind(g, positive);
    jump(g, g->print_unsigned);
  }
  if (g->print_unsigned) {
    static const int places[] = {10000, 1000, 100, 10, 1};
    bind(g, g->print_unsigned);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->print_work, 1); /* Leading digit already emitted? */
    for (j = 0; j < 5; ++j) {
      int loop = label(g), subtract = label(g), ready = label(g);
      int emit = label(g), done = label(g), place = places[j];
      ins(g, 0xa9, 0, 2);
      ref(g, 0x8d, g->print_work, 0);
      bind(g, loop);
      ins(g, 0xa5, RH, 2);
      ins(g, 0xc9, place >> 8, 2);
      branch(g, 0x90, ready);
      branch(g, 0xd0, subtract);
      ins(g, 0xa5, R, 2);
      ins(g, 0xc9, place & 255, 2);
      branch(g, 0x90, ready);
      bind(g, subtract);
      ins(g, 0x38, 0, 1);
      ins(g, 0xa5, R, 2);
      ins(g, 0xe9, place & 255, 2);
      ins(g, 0x85, R, 2);
      ins(g, 0xa5, RH, 2);
      ins(g, 0xe9, place >> 8, 2);
      ins(g, 0x85, RH, 2);
      ref(g, 0xee, g->print_work, 0);
      jump(g, loop);
      bind(g, ready);
      if (j != 4) {
        ref(g, 0xad, g->print_work, 0);
        branch(g, 0xd0, emit);
        ref(g, 0xad, g->print_work, 1);
        branch(g, 0xf0, done);
      }
      bind(g, emit);
      ins(g, 0xa9, 1, 2);
      ref(g, 0x8d, g->print_work, 1);
      ref(g, 0xad, g->print_work, 0);
      ins(g, 0x18, 0, 1);
      ins(g, 0x69, '0', 2);
      ins(g, 0x20, 0xffd2, 3);
      bind(g, done);
    }
    ins(g, 0x60, 0, 1);
  }
}
#include "reu_codegen.inc"
static void sprite_call(Backend *g, int builtin, int *args, int count) {
  int j;
  save_arguments(g, args, count);
  ref(g, 0xad, g->arguments, 0);
  ins(g, 0xc9, 8, 2);
  branch(g, 0xb0, g->trap);
  ins(g, 0xaa, 0, 1);
  if (builtin == BI_SPRITE_COLOR) {
    ref(g, 0xad, g->arguments, 2);
    ins(g, 0xc9, 16, 2);
    branch(g, 0xb0, g->trap);
    ins(g, 0x9d, 0xd027, 3);
    return;
  }
  if (builtin == BI_SPRITE_DATA) {
    ref(g, 0xad, g->arguments, 3);
    ins(g, 0xc9, 64, 2);
    branch(g, 0xb0, g->trap);
    ins(g, 0x85, RH, 2);
    ref(g, 0xad, g->arguments, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0x29, 63, 2);
    branch(g, 0xd0, g->trap);
    for (j = 0; j < 6; ++j) {
      ins(g, 0x46, RH, 2);
      ins(g, 0x66, R, 2);
    }
    ins(g, 0xa5, R, 2);
    ins(g, 0x9d, 0x07f8, 3);
    return;
  }
  if (!g->sprite_masks)
    g->sprite_masks = label(g);
  ref(g, 0xbd, g->sprite_masks, 0);
  ref(g, 0x8d, g->lhs, 0);
  if (builtin == BI_SPRITE_ENABLE || builtin == BI_SPRITE_DISABLE) {
    if (builtin == BI_SPRITE_ENABLE)
      ins(g, 0x0d, 0xd015, 3);
    else {
      ins(g, 0x49, 255, 2);
      ins(g, 0x2d, 0xd015, 3);
    }
    ins(g, 0x8d, 0xd015, 3);
  } else {
    int low = label(g), done = label(g);
    ref(g, 0xad, g->arguments, 3);
    ins(g, 0xc9, 2, 2);
    branch(g, 0xb0, g->trap);
    ins(g, 0xc9, 0, 2);
    branch(g, 0xf0, low);
    ref(g, 0xad, g->lhs, 0);
    ins(g, 0x0d, 0xd010, 3);
    jump(g, done);
    bind(g, low);
    ref(g, 0xad, g->lhs, 0);
    ins(g, 0x49, 255, 2);
    ins(g, 0x2d, 0xd010, 3);
    bind(g, done);
    ins(g, 0x8d, 0xd010, 3);
    ref(g, 0xad, g->arguments, 0);
    ins(g, 0x0a, 0, 1);
    ins(g, 0xaa, 0, 1);
    ref(g, 0xad, g->arguments, 2);
    ins(g, 0x9d, 0xd000, 3);
    ref(g, 0xad, g->arguments, 4);
    ins(g, 0x9d, 0xd001, 3);
  }
}
static void emit_string_helpers(Backend *g) {
  if (g->string_length) {
    bind(g, g->string_length);
    ins(g, 0x38, 0, 1);
    ins(g, 0xa5, P, 2);
    ins(g, 0xe9, 2, 2);
    ins(g, 0x85, P, 2);
    ins(g, 0xa5, PH, 2);
    ins(g, 0xe9, 0, 2);
    ins(g, 0x85, PH, 2);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xc8, 0, 1);
    ins(g, 0xb1, P, 2);
    ins(g, 0x85, RH, 2);
    ins(g, 0x60, 0, 1);
  }
  if (g->string_compare) {
    int work = g->string_work, loop = label(g), equal = label(g);
    int less = label(g), greater = label(g), done = label(g), j;
    bind(g, g->string_compare);
    bind(g, loop);
    ref(g, 0xad, work, 0);
    ins(g, 0x85, P, 2);
    ref(g, 0xad, work, 1);
    ins(g, 0x85, PH, 2);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ref(g, 0x8d, work, 4);
    ref(g, 0xad, work, 2);
    ins(g, 0x85, P, 2);
    ref(g, 0xad, work, 3);
    ins(g, 0x85, PH, 2);
    ins(g, 0xb1, P, 2);
    ref(g, 0xcd, work, 4);
    branch(g, 0xf0, equal);
    branch(g, 0x90, greater); /* Right byte is smaller. */
    jump(g, less);
    bind(g, equal);
    ins(g, 0xc9, 0, 2);
    branch(g, 0xf0, done);
    for (j = 0; j < 4; j += 2) {
      int advanced = label(g);
      ref(g, 0xee, work, j);
      branch(g, 0xd0, advanced);
      ref(g, 0xee, work, j + 1);
      bind(g, advanced);
    }
    jump(g, loop);
    bind(g, less);
    constant(g, -1);
    ins(g, 0x60, 0, 1);
    bind(g, greater);
    constant(g, 1);
    ins(g, 0x60, 0, 1);
    bind(g, done);
    constant(g, 0);
    ins(g, 0x60, 0, 1);
  }
}
/* Input strings retain their storage for this program entry. A bounded pool
   avoids overwriting values saved from repeated calls to the same function. */
static void emit_input_helper(Backend *g) {
  int poll, erase, finish, ascii, uppercase, store, carry, fits, copy, done;
  if (!g->input_line)
    return;
  poll = label(g);
  erase = label(g);
  finish = label(g);
  ascii = label(g);
  uppercase = label(g);
  store = label(g);
  carry = label(g);
  fits = label(g);
  copy = label(g);
  done = label(g);
  bind(g, g->input_line);
  ins(g, 0xa9, 0, 2);
  ref(g, 0x8d, g->input_work, 2); /* Edited line length. */
  bind(g, poll);
  ins(g, 0x20, 0xffe4, 3);
  ins(g, 0xc9, 0, 2); /* GETIN's flags are not part of its result contract. */
  branch(g, 0xf0, poll);
  ins(g, 0xc9, 13, 2);
  branch(g, 0xf0, finish);
  ins(g, 0xc9, 20, 2);
  branch(g, 0xf0, erase);
  ref(g, 0x8d, g->input_work, 3); /* Preserve PETSCII for echo. */
  ins(g, 0xc9, 32, 2);
  branch(g, 0x90, poll);
  ins(g, 0xc9, 96, 2);
  branch(g, 0x90, ascii);
  /* In the lowercase character set, shifted letters are $C1..$DA. */
  ins(g, 0xc9, 193, 2);
  branch(g, 0x90, poll);
  ins(g, 0xc9, 219, 2);
  branch(g, 0xb0, poll);
  ins(g, 0xad, 0xd018, 3);
  ins(g, 0x29, 2, 2);
  branch(g, 0xf0, poll);
  ref(g, 0xad, g->input_work, 3);
  ins(g, 0x29, 127, 2);
  jump(g, store);
  bind(g, ascii);
  ins(g, 0xc9, 'A', 2);
  branch(g, 0x90, store);
  ins(g, 0xc9, 'Z' + 1, 2);
  branch(g, 0xb0, store);
  ins(g, 0xad, 0xd018, 3);
  ins(g, 0x29, 2, 2);
  branch(g, 0xf0, uppercase);
  ref(g, 0xad, g->input_work, 3);
  ins(g, 0x09, 32, 2);
  jump(g, store);
  bind(g, uppercase);
  ref(g, 0xad, g->input_work, 3);
  bind(g, store);
  /* Set the pointer without losing the converted character. */
  ins(g, 0x48, 0, 1);
  set_pointer(g, g->input_buffer, 0);
  ref(g, 0xac, g->input_work, 2);
  ins(g, 0xc0, 255, 2);
  branch(g, 0xf0, g->trap);
  ins(g, 0x68, 0, 1);
  ins(g, 0x91, P, 2);
  ref(g, 0xee, g->input_work, 2);
  ref(g, 0xad, g->input_work, 3);
  ins(g, 0x20, 0xffd2, 3);
  jump(g, poll);
  bind(g, erase);
  ref(g, 0xad, g->input_work, 2);
  branch(g, 0xf0, poll);
  ref(g, 0xce, g->input_work, 2);
  output_character(g, 20);
  jump(g, poll);
  bind(g, finish);
  output_character(g, 13);
  set_pointer(g, g->input_buffer, 0);
  ref(g, 0xac, g->input_work, 2);
  ins(g, 0xa9, 0, 2);
  ins(g, 0x91, P, 2); /* Line terminator. */
  /* Compute the allocation end, checking the whole string before pool writes.
   */
  ins(g, 0x18, 0, 1);
  ref(g, 0xad, g->input_work, 0);
  ref(g, 0x6d, g->input_work, 2);
  ref(g, 0x8d, g->input_work, 6);
  ref(g, 0xad, g->input_work, 1);
  ins(g, 0x69, 0, 2);
  ref(g, 0x8d, g->input_work, 7);
  ins(g, 0x18, 0, 1);
  ref(g, 0xad, g->input_work, 6);
  ins(g, 0x69, 3, 2);
  ref(g, 0x8d, g->input_work, 6);
  ref(g, 0xad, g->input_work, 7);
  ins(g, 0x69, 0, 2);
  ref(g, 0x8d, g->input_work, 7);
  ins(g, 0xc9, 16, 2); /* 4096-byte arena, including headers and NULs. */
  branch(g, 0x90, fits);
  branch(g, 0xd0, g->trap);
  ref(g, 0xad, g->input_work, 6);
  branch(g, 0xd0, g->trap);
  bind(g, fits);
  set_pointer(g, g->input_pool, 0);
  ins(g, 0x18, 0, 1);
  ins(g, 0xa5, P, 2);
  ref(g, 0x6d, g->input_work, 0);
  ins(g, 0x85, P, 2);
  ins(g, 0xa5, PH, 2);
  ref(g, 0x6d, g->input_work, 1);
  ins(g, 0x85, PH, 2);
  ins(g, 0xa0, 0, 2);
  ref(g, 0xad, g->input_work, 2);
  ins(g, 0x91, P, 2);
  ins(g, 0xc8, 0, 1);
  ins(g, 0xa9, 0, 2);
  ins(g, 0x91, P, 2);
  ins(g, 0x18, 0, 1);
  ins(g, 0xa5, P, 2);
  ins(g, 0x69, 2, 2);
  ins(g, 0x85, P, 2);
  branch(g, 0x90, carry);
  ins(g, 0xe6, PH, 2);
  bind(g, carry);
  ins(g, 0xa5, P, 2);
  ref(g, 0x8d, g->input_work, 4);
  ins(g, 0xa5, PH, 2);
  ref(g, 0x8d, g->input_work, 5);
  ins(g, 0xa2, 0, 2);
  ins(g, 0xa0, 0, 2);
  bind(g, copy);
  ref(g, 0xbd, g->input_buffer, 0);
  ins(g, 0x91, P, 2);
  branch(g, 0xf0, done);
  ins(g, 0xe8, 0, 1);
  ins(g, 0xe6, P, 2);
  branch(g, 0xd0, copy);
  ins(g, 0xe6, PH, 2);
  jump(g, copy);
  bind(g, done);
  ref(g, 0xad, g->input_work, 6);
  ref(g, 0x8d, g->input_work, 0);
  ref(g, 0xad, g->input_work, 7);
  ref(g, 0x8d, g->input_work, 1);
  ref(g, 0xad, g->input_work, 4);
  ins(g, 0x85, R, 2);
  ref(g, 0xad, g->input_work, 5);
  ins(g, 0x85, RH, 2);
  ins(g, 0x60, 0, 1);
}
static void file_call(Backend *g, int builtin, int *args, int count) {
  int failed, finish, done;
  if (builtin == BI_FILE_STATUS || builtin == BI_FILE_ERROR) {
    ref(g, 0xad, g->file_work, builtin == BI_FILE_ERROR);
    ins(g, 0x85, R, 2);
    normalize(g, TY_U8);
    return;
  }
  failed = label(g);
  finish = label(g);
  done = label(g);
  save_arguments(g, args, count);
  ins(g, 0xa9, 0, 2);
  ins(g, 0x85, 0x90, 2); /* Start a fresh KERNAL status for this operation. */
  ref(g, 0x8d, g->file_work, 1);
  ref(g, 0x8d, g->file_work, 2);
  if (builtin == BI_FILE_OPEN) {
    int loop = label(g), named = label(g);
    if (!g->file_name)
      g->file_name = label(g);
    ref(g, 0xad, g->arguments, 6);
    ins(g, 0x85, P, 2);
    ref(g, 0xad, g->arguments, 7);
    ins(g, 0x85, PH, 2);
    ins(g, 0xa0, 0, 2);
    bind(g, loop);
    ins(g, 0xb1, P, 2);
    branch(g, 0xf0, named);
    to_petscii(g);
    ref(g, 0x99, g->file_name, 0);
    ins(g, 0xc8, 0, 1);
    branch(g, 0xd0, loop);
    jump(g, g->trap); /* SETNAM's length cannot represent 256 bytes. */
    bind(g, named);
    set_pointer(g, g->file_name, 0);
    ins(g, 0x98, 0, 1);
    ins(g, 0xa6, P, 2);
    ins(g, 0xa4, PH, 2);
    ins(g, 0x20, 0xffbd, 3); /* SETNAM */
    ref(g, 0xad, g->arguments, 0);
    ref(g, 0xae, g->arguments, 2);
    ref(g, 0xac, g->arguments, 4);
    ins(g, 0x20, 0xffba, 3); /* SETLFS */
    ins(g, 0x20, 0xffc0, 3); /* OPEN */
    branch(g, 0xb0, failed);
    ins(g, 0xa9, 1, 2);
    ref(g, 0x8d, g->file_work, 2);
  } else if (builtin == BI_FILE_CLOSE) {
    ref(g, 0xad, g->arguments, 0);
    ins(g, 0x20, 0xffc3, 3); /* CLOSE: carry is not a success flag. */
  } else {
    ref(g, 0xae, g->arguments, 0);
    ins(g, 0x20, builtin == BI_FILE_READ ? 0xffc6 : 0xffc9, 3);
    branch(g, 0xb0, failed); /* CHKIN / CHKOUT */
    if (builtin == BI_FILE_READ) {
      ins(g, 0x20, 0xffcf, 3); /* CHRIN: save byte before READST/CLRCHN. */
    } else if (builtin == BI_FILE_TEXT) {
      int loop = label(g), end = label(g);
      ref(g, 0xad, g->arguments, 2);
      ref(g, 0x8d, g->file_work, 3);
      ref(g, 0xad, g->arguments, 3);
      ref(g, 0x8d, g->file_work, 4);
      bind(g, loop);
      /* Reload the pointer and Y across ROM calls; no register survives I/O. */
      ref(g, 0xad, g->file_work, 3);
      ins(g, 0x85, P, 2);
      ref(g, 0xad, g->file_work, 4);
      ins(g, 0x85, PH, 2);
      ins(g, 0xa0, 0, 2);
      ins(g, 0xb1, P, 2);
      branch(g, 0xf0, end);
      to_petscii(g);
      ins(g, 0x20, 0xffd2, 3);
      ins(g, 0x20, 0xffb7, 3);
      ins(g, 0xc9, 0, 2);
      branch(g, 0xd0, finish);
      ref(g, 0xee, g->file_work, 3);
      branch(g, 0xd0, loop);
      ref(g, 0xee, g->file_work, 4);
      jump(g, loop);
      bind(g, end);
      ins(g, 0xa9, 1, 2);
    } else {
      ref(g, 0xad, g->arguments, 2);
      ins(g, 0x20, 0xffd2, 3); /* CHROUT: errors are in READST. */
      ins(g, 0xa9, 1, 2);
    }
    ref(g, 0x8d, g->file_work, 2);
  }
  jump(g, finish);
  bind(g, failed);
  ref(g, 0x8d, g->file_work, 1); /* OPEN / channel-selection error in A. */
  bind(g, finish);
  ins(g, 0x20, 0xffb7, 3); /* READST */
  ref(g, 0x8d, g->file_work, 0);
  ins(g, 0x20, 0xffcc, 3); /* CLRCHN also flushes the final serial byte. */
  ins(g, 0x20, 0xffb7, 3);
  ref(g, 0x0d, g->file_work, 0);
  ref(g, 0x8d, g->file_work, 0);
  if (builtin == BI_FILE_OPEN || builtin == BI_FILE_WRITE ||
      builtin == BI_FILE_TEXT) {
    branch(g, 0xf0, done);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->file_work, 2);
  }
  bind(g, done);
  ref(g, 0xad, g->file_work, 2);
  ins(g, 0x85, R, 2);
  normalize(g, TY_U8);
}
static void zero_symbol(Backend *g, Symbol *s);
static void initialize_object(Backend *g, Node *n) {
  Compiler *c = g->c;
  int object = n->c - 1, field;
  ClassLayout *layout = &c->classes[n->type - TY_CLASS_BASE];
  zero_symbol(g, &c->symbols[object]);
  for (field = 0; field < c->nsymbols; ++field) {
    Symbol *s = &c->symbols[field];
    if (s->parent != object + 1)
      continue;
    Node *definition = &c->nodes[layout->fields[s->count]];
    if (definition->b)
      expr(g, definition->b);
    else if (s->type == TY_STR || s->type == TY_BYTES) {
      if (s->type == TY_BYTES && !g->bytes_label)
        g->bytes_label = label(g);
      set_pointer(g, s->type == TY_BYTES ? g->bytes_label : g->string_label, 2);
      ins(g, 0xa5, P, 2);
      ins(g, 0x85, R, 2);
      ins(g, 0xa5, PH, 2);
      ins(g, 0x85, RH, 2);
    } else if (s->type == TY_FLOAT) {
      int k;
      ins(g, 0xa9, 0, 2);
      for (k = 0; k < 5; ++k)
        ref(g, 0x8d, g->float_result, k);
      float_pointer(g, g->float_result, 0);
    } else
      constant(g, 0);
    store_symbol(g, field);
  }
}
static void call(Backend *g, Node *n) {
  Compiler *c = g->c;
  int args[16], count = 0, j, k;
  for (j = n->b; j; j = c->nodes[j].next)
    args[count++] = j;
  if (n->kind == N_CONSTRUCT && n->symbol < 0) {
    initialize_object(g, n);
    return;
  }
  if (n->symbol >= 0) {
    Function *f = &c->functions[n->symbol];
    for (j = 0; j < count; ++j) {
      Symbol *parameter = &c->symbols[f->param[j]];
      if (parameter->reference && !object_type(parameter->type)) {
        Symbol *actual = &c->symbols[c->nodes[args[j]].symbol];
        if (actual->reference) {
          ref(g, 0xad, actual->label, 0);
          ins(g, 0x85, R, 2);
          ref(g, 0xad, actual->label, 1);
          ins(g, 0x85, RH, 2);
        } else {
          set_pointer(g, actual->label, 0);
          ins(g, 0xa5, P, 2);
          ins(g, 0x85, R, 2);
          ins(g, 0xa5, PH, 2);
          ins(g, 0x85, RH, 2);
        }
        push(g);
        if (actual->reference) {
          ref(g, 0xad, actual->label, 2);
          ins(g, 0x85, R, 2);
          ref(g, 0xad, actual->label, 3);
          ins(g, 0x85, RH, 2);
        } else
          constant(g, actual->count);
        push(g);
      } else {
        expr(g, args[j]);
        if (parameter->type == TY_FLOAT)
          float_push(g);
        else
          push(g);
      }
    }
    if (n->kind == N_CONSTRUCT)
      initialize_object(g, n);
    for (j = count - 1; j >= 0; --j) {
      Symbol *parameter = &c->symbols[f->param[j]];
      if (parameter->type == TY_FLOAT) {
        float_pop(g, parameter->label, 0);
        continue;
      }
      if (parameter->reference && !object_type(parameter->type)) {
        ins(g, 0x68, 0, 1);
        ref(g, 0x8d, parameter->label, 3);
        if (parameter->count >= 0) {
          ins(g, 0xc9, parameter->count >> 8, 2);
          branch(g, 0xd0, g->trap);
        }
        ins(g, 0x68, 0, 1);
        ref(g, 0x8d, parameter->label, 2);
        if (parameter->count >= 0) {
          ins(g, 0xc9, parameter->count & 255, 2);
          branch(g, 0xd0, g->trap);
        }
        ins(g, 0x68, 0, 1);
        ref(g, 0x8d, parameter->label, 1);
        ins(g, 0x68, 0, 1);
        ref(g, 0x8d, parameter->label, 0);
        continue;
      }
      ins(g, 0x68, 0, 1);
      ins(g, 0x85, RH, 2);
      ins(g, 0x68, 0, 1);
      ins(g, 0x85, R, 2);
      store_symbol(g, f->param[j]);
    }
    ref(g, 0x20, f->label, 0);
    normalize(g, n->type);
    return;
  }
  k = -n->symbol;
  if (k >= BI_MATH_POW && k <= BI_MATH_TRUNC) {
    static const int routines[] = {0xbf7b, 0xbf71, 0xbfed, 0xb9ea,
                                   0xe26b, 0xe264, 0xe2b4, 0xe30e};
    if (k == BI_MATH_POW) {
      Node power = *n;
      power.op = T_POWER;
      power.a = args[0];
      power.b = args[1];
      float_binary(g, &power);
    } else if (k == BI_MATH_FABS) {
      expr(g, args[0]);
      float_copy(g, g->float_result, 0);
      ref(g, 0xad, g->float_result, 1);
      ins(g, 0x29, 127, 2);
      ref(g, 0x8d, g->float_result, 1);
      float_pointer(g, g->float_result, 0);
    } else if (k >= BI_MATH_FLOOR) {
      expr(g, args[0]);
      if (k != BI_MATH_TRUNC) {
        if (k == BI_MATH_CEIL)
          float_negate(g);
        float_fac(g, TY_FLOAT);
        ins(g, 0x20, 0xbccc,
            3); /* BASIC INT rounds toward negative infinity. */
        float_finish(g);
        if (k == BI_MATH_CEIL)
          float_negate(g);
      }
      float_integer_convert(g, 1);
      normalize(g, TY_I16);
    } else {
      expr(g, args[0]);
      float_fac(g, TY_FLOAT);
      ins(g, 0x20, routines[k - BI_MATH_POW], 3);
      float_finish(g);
    }
    return;
  }
  if (k == BI_INPUT) {
    if (count) {
      expr(g, args[0]);
      if (!g->print_string)
        g->print_string = label(g);
      ref(g, 0x20, g->print_string, 0);
    }
    ref(g, 0x20, g->input_line, 0);
    return;
  }
  if (k == BI_BYTES) {
    bytes_constructor(g, n);
    return;
  }
  if (k == BI_INT) {
    expr(g, args[0]);
    if (c->nodes[args[0]].type == TY_STR) {
      float_parse_text(g, 1);
      float_finish(g);
      float_integer_convert(g, 1);
    } else if (c->nodes[args[0]].type == TY_FLOAT)
      float_integer_convert(g, 1);
    normalize(g, n->type);
    return;
  }
  if (k == BI_LEN) {
    if (c->nodes[args[0]].type == TY_STR ||
        c->nodes[args[0]].type == TY_BYTES) {
      expr(g, args[0]);
      ins(g, 0xa5, R, 2);
      ins(g, 0x85, P, 2);
      ins(g, 0xa5, RH, 2);
      ins(g, 0x85, PH, 2);
      length_call(g);
      return;
    }
    Symbol *s = &c->symbols[c->nodes[args[0]].symbol];
    ref(g, 0xad, s->label, 2);
    ins(g, 0x85, R, 2);
    ref(g, 0xad, s->label, 3);
    ins(g, 0x85, RH, 2);
    return;
  }
  if (k == BI_ORD) {
    expr(g, args[0]);
    if (c->nodes[args[0]].type == TY_BYTES) {
      int work = bytes_workspace(g);
      for (j = 0; j < 2; ++j) {
        ins(g, 0xa5, R + j, 2);
        ref(g, 0x8d, work, j);
        ins(g, 0x85, P + j, 2);
      }
      length_call(g);
      ins(g, 0xa5, RH, 2);
      branch(g, 0xd0, g->trap);
      ins(g, 0xa5, R, 2);
      ins(g, 0xc9, 1, 2);
      branch(g, 0xd0, g->trap);
      for (j = 0; j < 2; ++j) {
        ref(g, 0xad, work, j);
        ins(g, 0x85, P + j, 2);
      }
      ins(g, 0xa0, 0, 2);
      ins(g, 0xb1, P, 2);
      ins(g, 0x85, R, 2);
      normalize(g, TY_U8);
      return;
    }
    truth(g, TY_STR);
    branch(g, 0xf0, g->trap);
    ins(g, 0x85, R, 2);
    ins(g, 0xc8, 0, 1);
    ins(g, 0xb1, P, 2);
    branch(g, 0xd0, g->trap);
    normalize(g, TY_U8);
    return;
  }
  if (k == BI_ASSET_ADDRESS) {
    expr(g, args[0]);
    return;
  }
  if (k >= BI_MUSIC_START && k <= BI_MUSIC_TICKS) {
    music_call(g, k, args);
    return;
  }
  if (k == BI_VIDEO_MODE) {
    int text = label(g), ready = label(g), high = label(g);
    expr(g, args[0]);
    ins(g, 0xa5, RH, 2);
    branch(g, 0xd0, g->trap);
    ins(g, 0xa5, R, 2);
    ins(g, 0xc9, 2, 2);
    branch(g, 0xb0, g->trap);
    ins(g, 0x08, 0, 1); /* Preserve the caller's IRQ flag. */
    ins(g, 0x78, 0, 1);
    ref(g, 0x8d, g->math, 0);
    ins(g, 0xad, 0xd011, 3);
    ins(g, 0x29, 0x80, 2);
    ref(g, 0x8d, g->math, 1);
    if (g->irq_work) {
      ref(g, 0xad, g->irq_work, 2);
      branch(g, 0xf0, high);
      ref(g, 0xad, g->irq_work, 7); /* Compare latch, not beam position. */
      ref(g, 0x8d, g->math, 1);
      bind(g, high);
    }
    ins(g, 0xad, 0xd011, 3);
    ins(g, 0x29, 7, 2);
    ins(g, 0x09, 0x18, 2);
    ref(g, 0x0d, g->math, 1);
    ref(g, 0x8d, g->math, 1);
    ref(g, 0xad, g->math, 0);
    branch(g, 0xf0, text);
    ref(g, 0xad, g->math, 1);
    ins(g, 0x09, 0x20, 2);
    ins(g, 0x8d, 0xd011, 3);
    ins(g, 0xa9, 0x19, 2);
    jump(g, ready);
    bind(g, text);
    ref(g, 0xad, g->math, 1);
    ins(g, 0x8d, 0xd011, 3);
    ins(g, 0xa9, 0x15, 2);
    bind(g, ready);
    ins(g, 0x8d, 0xd018, 3);
    ins(g, 0xad, 0xd016, 3);
    ins(g, 0x29, 0xef, 2);
    ins(g, 0x09, 8, 2);
    ins(g, 0x8d, 0xd016, 3);
    ins(g, 0x28, 0, 1);
    return;
  }
  if (k == BI_CHR) {
    expr(g, args[0]);
    ins(g, 0xa5, RH, 2);
    branch(g, 0xd0, g->trap);
    ins(g, 0xa5, R, 2);
    branch(g, 0xf0, g->trap);
    ins(g, 0xc9, 128, 2);
    branch(g, 0xb0, g->trap);
    character(g);
    return;
  }
  if (k > BI_CAST_BASE) {
    if (n->type == TY_FLOAT) {
      float_convert(g, args[0]);
      return;
    }
    expr(g, args[0]);
    if (n->type == TY_BOOL)
      boolean(g, c->nodes[args[0]].type);
    else {
      if (c->nodes[args[0]].type == TY_FLOAT)
        float_integer_convert(g, 0);
      normalize(g, n->type);
    }
    return;
  }
  if (k == BI_PRINT) {
    int floating = 0;
    for (j = 0; j < count; ++j)
      if (c->nodes[args[j]].type == TY_FLOAT)
        floating = 1;
    if (floating) {
      if (!g->float_arguments)
        g->float_arguments = label(g);
      if (count > g->float_argument_count)
        g->float_argument_count = count;
      for (j = 0; j < count; ++j) {
        expr(g, args[j]);
        if (c->nodes[args[j]].type == TY_FLOAT)
          float_push(g);
        else
          push(g);
      }
      for (j = count - 1; j >= 0; --j) {
        if (c->nodes[args[j]].type == TY_FLOAT)
          float_pop(g, g->float_arguments, j * 5);
        else {
          ins(g, 0x68, 0, 1);
          ref(g, 0x8d, g->float_arguments, j * 5 + 1);
          ins(g, 0x68, 0, 1);
          ref(g, 0x8d, g->float_arguments, j * 5);
        }
      }
    } else
      save_arguments(g, args, count);
    for (j = 0; j < count; ++j) {
      int type = c->nodes[args[j]].type, *routine;
      if (j)
        output_character(g, ' ');
      if (type == TY_FLOAT)
        float_pointer(g, g->float_arguments, j * 5);
      else {
        ref(g, 0xad, floating ? g->float_arguments : g->arguments,
            j * (floating ? 5 : 2));
        ins(g, 0x85, R, 2);
        ref(g, 0xad, floating ? g->float_arguments : g->arguments,
            j * (floating ? 5 : 2) + 1);
        ins(g, 0x85, RH, 2);
      }
      if (type == TY_STR)
        routine = &g->print_string;
      else if (type == TY_BYTES) {
        bytes_workspace(g);
        routine = &g->print_bytes;
      } else if (type == TY_BOOL)
        routine = &g->print_bool;
      else if (type == TY_FLOAT)
        routine = &g->print_float;
      else {
        if (!g->print_unsigned) {
          g->print_unsigned = label(g);
          g->print_work = label(g);
        }
        routine = type_signed(type) ? &g->print_signed : &g->print_unsigned;
      }
      if (!*routine)
        *routine = label(g);
      ref(g, 0x20, *routine, 0);
    }
    output_character(g, 13);
    return;
  }
  if (k == BI_KEY_READ) {
    ins(g, 0x20, 0xffe4, 3);
    ins(g, 0x85, R, 2);
    normalize(g, TY_U8);
    return;
  }
  if (k == BI_KEY_CLEAR) {
    ins(g, 0xa9, 0, 2);
    ins(g, 0x85, 0xc6, 2);
    return;
  }
  if (k >= BI_SPRITE_ENABLE && k <= BI_SPRITE_DATA) {
    sprite_call(g, k, args, count);
    return;
  }
  if (k >= BI_FILE_OPEN && k <= BI_FILE_ERROR) {
    file_call(g, k, args, count);
    return;
  }
  if (k == BI_REU_PRESENT || k == BI_REU_TRANSFER) {
    reu_call(g, k, args, count);
    return;
  }
  if (k >= BI_IRQ_INSTALL && k <= BI_IRQ_RESTORE) {
    irq_call(g, k, n, args);
    return;
  }
  if (k == BI_CLEAR) {
    int loop = label(g);
    ins(g, 0xa2, 0, 2);
    ins(g, 0xa9, 32, 2);
    bind(g, loop);
    ins(g, 0x9d, 0x0400, 3);
    ins(g, 0x9d, 0x0500, 3);
    ins(g, 0x9d, 0x0600, 3);
    ins(g, 0x9d, 0x06e8, 3);
    ins(g, 0xe8, 0, 1);
    branch(g, 0xd0, loop);
    /* Color RAM is set to white for the default screen. */
    ins(g, 0xa9, 1, 2);
    bind(g, loop = label(g));
    ins(g, 0x9d, 0xd800, 3);
    ins(g, 0x9d, 0xd900, 3);
    ins(g, 0x9d, 0xda00, 3);
    ins(g, 0x9d, 0xdae8, 3);
    ins(g, 0xe8, 0, 1);
    branch(g, 0xd0, loop);
    return;
  }
  if (k == BI_PEEK || k == BI_POKE) {
    expr(g, args[0]);
    if (k == BI_POKE) {
      push(g);
      expr(g, args[1]);
      pop_lhs(g);
      ref(g, 0xad, g->lhs, 0);
      ins(g, 0x85, P, 2);
      ref(g, 0xad, g->lhs, 1);
      ins(g, 0x85, PH, 2);
      ins(g, 0xa0, 0, 2);
      ins(g, 0xa5, R, 2);
      ins(g, 0x91, P, 2);
    } else {
      ins(g, 0xa5, R, 2);
      ins(g, 0x85, P, 2);
      ins(g, 0xa5, RH, 2);
      ins(g, 0x85, PH, 2);
      ins(g, 0xa0, 0, 2);
      ins(g, 0xb1, P, 2);
      ins(g, 0x85, R, 2);
      normalize(g, TY_U8);
    }
    return;
  }
  if (k == BI_PUTCHAR) {
    int loop = label(g), end = label(g);
    expr(g, args[0]);
    push(g);
    expr(g, args[1]);
    pop_lhs(g);
    ref(g, 0xad, g->lhs, 0);
    ins(g, 0xc9, 40, 2);
    branch(g, 0xb0, g->trap);
    ins(g, 0xa5, R, 2);
    ins(g, 0xc9, 25, 2);
    branch(g, 0xb0, g->trap);
    ins(g, 0xaa, 0, 1);
    ref(g, 0xad, g->lhs, 0);
    ins(g, 0x85, P, 2);
    ins(g, 0xa9, 4, 2);
    ins(g, 0x85, PH, 2);
    bind(g, loop);
    ins(g, 0x8a, 0, 1);
    branch(g, 0xf0, end);
    ins(g, 0x18, 0, 1);
    ins(g, 0xa5, P, 2);
    ins(g, 0x69, 40, 2);
    ins(g, 0x85, P, 2);
    ins(g, 0xa5, PH, 2);
    ins(g, 0x69, 0, 2);
    ins(g, 0x85, PH, 2);
    ins(g, 0xca, 0, 1);
    jump(g, loop);
    bind(g, end);
    /* Preserve the address while evaluating the final argument. */
    ins(g, 0xa5, P, 2);
    ins(g, 0x48, 0, 1);
    ins(g, 0xa5, PH, 2);
    ins(g, 0x48, 0, 1);
    if (c->nodes[args[2]].kind == N_STRING)
      constant(g, screen_code(c->strings[c->nodes[args[2]].value]));
    else
      expr(g, args[2]);
    ins(g, 0x68, 0, 1);
    ins(g, 0x85, PH, 2);
    ins(g, 0x68, 0, 1);
    ins(g, 0x85, P, 2);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xa5, R, 2);
    ins(g, 0x91, P, 2);
    return;
  }
}
static void enum_lookup(Backend *g, Node *n) {
  Compiler *c = g->c;
  EnumLayout *e = &c->enums[(n->c - TY_ENUM_BASE) / 4];
  int j, k, done = label(g), work;
  expr(g, n->a);
  if (!g->enum_work)
    g->enum_work = label(g);
  for (k = 0; k < 2; ++k) {
    ins(g, 0xa5, R + k, 2);
    ref(g, 0x8d, g->enum_work, k);
  }
  if (!n->op && type_signed(c->nodes[n->a].type) != type_signed(n->c)) {
    ins(g, 0xa5, RH, 2);
    branch(g, 0x30, g->trap); /* Opposite signs must not alias the same bits. */
  }
  for (j = e->first; j < e->first + e->count; ++j) {
    EnumMember *m = &c->enum_members[j];
    int next;
    if (n->op != 2 && m->canonical != j)
      continue;
    next = label(g);
    if (n->op == 2) {
      work = string_workspace(g);
      if (!g->string_compare)
        g->string_compare = label(g);
      for (k = 0; k < 2; ++k) {
        ref(g, 0xad, g->enum_work, k);
        ref(g, 0x8d, work, k);
      }
      set_pointer(g, string_literal_label(g, m->string - 1), 2);
      ins(g, 0xa5, P, 2);
      ref(g, 0x8d, work, 2);
      ins(g, 0xa5, PH, 2);
      ref(g, 0x8d, work, 3);
      ref(g, 0x20, g->string_compare, 0);
      ins(g, 0xa5, R, 2);
      ins(g, 0x05, RH, 2);
      branch(g, 0xd0, next);
    } else {
      ref(g, 0xad, g->enum_work, 0);
      ins(g, 0xc9, (int)m->value & 255, 2);
      branch(g, 0xd0, next);
      ref(g, 0xad, g->enum_work, 1);
      ins(g, 0xc9, (int)(m->value & 65535) >> 8, 2);
      branch(g, 0xd0, next);
    }
    if (n->op == 1) {
      set_pointer(g, string_literal_label(g, m->string - 1), 2);
      ins(g, 0xa5, P, 2);
      ins(g, 0x85, R, 2);
      ins(g, 0xa5, PH, 2);
      ins(g, 0x85, RH, 2);
    } else
      constant(g, m->value);
    jump(g, done);
    bind(g, next);
  }
  jump(g, g->trap);
  bind(g, done);
}
static void expr(Backend *g, int i) {
  Node *n = &g->c->nodes[i];
  switch (n->kind) {
  case N_CONST:
    constant(g, n->value);
    break;
  case N_ENUM_LOOKUP:
    enum_lookup(g, n);
    break;
  case N_ENUM_VALUE:
    expr(g, n->a);
    break;
  case N_STRING:
    set_pointer(g, string_literal_label(g, (int)n->value), 2);
    ins(g, 0xa5, P, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa5, PH, 2);
    ins(g, 0x85, RH, 2);
    break;
  case N_BYTES:
    set_pointer(g, byte_literal_label(g, (int)n->value), 2);
    ins(g, 0xa5, P, 2);
    ins(g, 0x85, R, 2);
    ins(g, 0xa5, PH, 2);
    ins(g, 0x85, RH, 2);
    break;
  case N_FLOAT:
    if (!g->float_literals[i])
      g->float_literals[i] = label(g);
    float_pointer(g, g->float_literals[i], 0);
    break;
  case N_FLOAT_CAST:
    float_convert(g, n->a);
    break;
  case N_NAME:
  case N_FIELD:
    load_symbol(g, n->symbol);
    break;
  case N_HW:
    ins(g, 0xad, (int)n->value, 3);
    if (n->d) {
      ins(g, 0x29, n->d, 2);
      ins(g, 0x85, R, 2);
      ins(g, 0xa9, 0, 2);
      ins(g, 0x85, RH, 2);
      boolean(g, TY_U8);
      ins(g, 0xa5, R, 2);
      ins(g, 0x49, 1, 2);
    }
    ins(g, 0x85, R, 2);
    normalize(g, n->type);
    break;
  case N_BYTES_SEARCH:
    bytes_search_expression(g, n);
    break;
  case N_SLICE:
    bytes_slice_expression(g, n);
    break;
  case N_INDEX:
    if (g->c->nodes[n->a].type == TY_STR ||
        g->c->nodes[n->a].type == TY_BYTES) {
      string_index(g, n);
      break;
    }
    address(g, i);
    if (n->type == TY_FLOAT) {
      ins(g, 0xa5, P, 2);
      ins(g, 0x85, R, 2);
      ins(g, 0xa5, PH, 2);
      ins(g, 0x85, RH, 2);
      break;
    }
    ins(g, 0xa0, 0, 2);
    ins(g, 0xb1, P, 2);
    ins(g, 0x85, R, 2);
    if (type_size(n->type) == 2) {
      ins(g, 0xc8, 0, 1);
      ins(g, 0xb1, P, 2);
      ins(g, 0x85, RH, 2);
    } else
      normalize(g, n->type);
    break;
  case N_CALL:
  case N_CONSTRUCT:
  case N_PROPERTY_CALL:
    call(g, n);
    break;
  case N_BINARY:
    binary(g, n);
    break;
  case N_UNARY:
    expr(g, n->a);
    if (n->op == '-') {
      if (n->type == TY_FLOAT) {
        float_negate(g);
        break;
      }
      ins(g, 0x38, 0, 1);
      ins(g, 0xa9, 0, 2);
      ins(g, 0xe5, R, 2);
      ins(g, 0x85, R, 2);
      ins(g, 0xa9, 0, 2);
      ins(g, 0xe5, RH, 2);
      ins(g, 0x85, RH, 2);
    } else if (n->op == '~') {
      ins(g, 0xa5, R, 2);
      ins(g, 0x49, 255, 2);
      ins(g, 0x85, R, 2);
      ins(g, 0xa5, RH, 2);
      ins(g, 0x49, 255, 2);
      ins(g, 0x85, RH, 2);
    } else if (n->op == OP_NOT) {
      boolean(g, g->c->nodes[n->a].type);
      ins(g, 0xa5, R, 2);
      ins(g, 0x49, 1, 2);
      ins(g, 0x85, R, 2);
    }
    normalize(g, n->type);
    break;
  default:
    fail(g->c, n->token, "internal error: expression was not lowered");
  }
}
static void block(Backend *g, int i);
static void zero_symbol(Backend *g, Symbol *s);
static int snapshot_list(Backend *g, int i) {
  Node *value = &g->c->nodes[i];
  int item, offset = 0, width = type_size(value->c), temporary = label(g);
  g->list_labels[i] = temporary;
  if (g->in_irq_function)
    g->irq_temps[i] = 1;
  for (item = value->a; item; item = g->c->nodes[item].next) {
    expr(g, item);
    if (value->c == TY_FLOAT) {
      float_copy(g, temporary, offset);
      offset += width;
      continue;
    }
    ins(g, 0xa5, R, 2);
    ref(g, 0x8d, temporary, offset);
    if (width == 2) {
      ins(g, 0xa5, RH, 2);
      ref(g, 0x8d, temporary, offset + 1);
    }
    offset += width;
  }
  return temporary;
}
#include "bytes_codegen.inc"
#include "bytes_search_codegen.inc"
// clang-format off
#include "music_codegen.inc"
#include "irq_codegen.inc"
// clang-format on
static void assignment(Backend *g, Node *n) {
  Node *lhs = &g->c->nodes[n->a];
  if (!n->b)
    return;
  if (object_type(n->type)) {
    call(g, &g->c->nodes[n->b]);
    return;
  }
  if (n->type == TY_ARRAY) {
    zero_symbol(g, &g->c->symbols[lhs->symbol]);
    return;
  }
  if (n->type == TY_LIST) {
    Symbol *target = &g->c->symbols[lhs->symbol];
    /* Snapshot every element before changing the old list's storage. */
    int offset, width = type_size(target->element),
                temporary = snapshot_list(g, n->b);
    for (offset = 0; offset < target->count * width; ++offset) {
      ref(g, 0xad, temporary, offset);
      ref(g, 0x8d, target->label, offset);
    }
    return;
  }
  /* Real 6502 peephole: x += 1 and x -= 1 become INC/DEC for u8/i8. */
  if (n->op && lhs->kind == N_NAME && type_size(n->type) == 1) {
    Node *bin = &g->c->nodes[n->b], *rhs = &g->c->nodes[bin->b];
    if (rhs->kind == N_CONST && rhs->value == 1 &&
        (bin->op == '+' || bin->op == '-')) {
      ref(g, bin->op == '+' ? 0xee : 0xce, g->c->symbols[lhs->symbol].label, 0);
      return;
    }
  }
  if (lhs->kind == N_INDEX) {
    expr(g, n->b);
    if (n->type == TY_FLOAT) {
      int j;
      float_push(g);
      address(g, n->a);
      for (j = 4; j >= 0; --j) {
        ins(g, 0x68, 0, 1);
        ins(g, 0xa0, j, 2);
        ins(g, 0x91, P, 2);
      }
      return;
    }
    push(g); /* Python evaluates the RHS before the target's index. */
    address(g, n->a);
    ins(g, 0x68, 0, 1);
    ins(g, 0x85, RH, 2);
    ins(g, 0x68, 0, 1);
    ins(g, 0x85, R, 2);
    ins(g, 0xa0, 0, 2);
    ins(g, 0xa5, R, 2);
    ins(g, 0x91, P, 2);
    if (type_size(n->type) == 2) {
      ins(g, 0xc8, 0, 1);
      ins(g, 0xa5, RH, 2);
      ins(g, 0x91, P, 2);
    }
    return;
  }
  expr(g, n->b);
  if (lhs->kind == N_NAME || lhs->kind == N_FIELD)
    store_symbol(g, lhs->symbol);
  else {
    ins(g, 0xa5, R, 2);
    ins(g, 0x8d, (int)lhs->value, 3);
  }
}
static void guard_loop_value(Backend *g, int type) {
  type = integer_type(type);
  if (type == TY_I16)
    return;
  ins(g, 0xa5, RH, 2);
  if (type == TY_U16) {
    branch(g, 0x30, g->trap);
  } else if (type == TY_I8) {
    int positive = label(g), done = label(g);
    branch(g, 0xf0, positive);
    ins(g, 0xc9, 255, 2);
    branch(g, 0xd0, g->trap);
    ins(g, 0xa5, R, 2);
    ins(g, 0xc9, 128, 2);
    branch(g, 0x90, g->trap);
    jump(g, done);
    bind(g, positive);
    ins(g, 0xa5, R, 2);
    ins(g, 0xc9, 128, 2);
    branch(g, 0xb0, g->trap);
    bind(g, done);
  } else {
    branch(g, 0xd0, g->trap);
    if (type == TY_BOOL) {
      ins(g, 0xa5, R, 2);
      ins(g, 0xc9, 2, 2);
      branch(g, 0xb0, g->trap);
    }
  }
}
static void capture_range(Backend *g, Node *n, int storage) {
  int args[3] = {n->b, n->c, n->d}, j, count = n->d ? 3 : n->c ? 2 : 1;
  for (j = 0; j < count; ++j) {
    expr(g, args[j]);
    push(g);
  }
  for (j = count - 1; j >= 0; --j) {
    int offset = count == 1 ? 2 : j * 2;
    ins(g, 0x68, 0, 1);
    ref(g, 0x8d, storage, offset + 1);
    ins(g, 0x68, 0, 1);
    ref(g, 0x8d, storage, offset);
  }
  for (j = 0; j < count; ++j)
    if (integer_type(g->c->nodes[args[j]].type) == TY_U16) {
      ref(g, 0xad, storage, (count == 1 ? 2 : j * 2) + 1);
      branch(g, 0x30, g->trap);
    }
  if (count == 1) {
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, storage, 0);
    ref(g, 0x8d, storage, 1);
  }
  if (count < 3) {
    ins(g, 0xa9, 1, 2);
    ref(g, 0x8d, storage, 4);
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, storage, 5);
  }
  ref(g, 0xad, storage, 4);
  ref(g, 0x0d, storage, 5);
  branch(g, 0xf0, g->trap);
}
static void for_loop(Backend *g, int i) {
  Node *n = &g->c->nodes[i];
  long start = 0, stop = g->c->nodes[n->b].value, step = 1;
  int induction = g->for_labels[i], head = label(g), cont = label(g),
      exhausted = label(g), end = label(g);
  if (n->c) {
    start = stop;
    stop = g->c->nodes[n->c].value;
  }
  if (n->d)
    step = g->c->nodes[n->d].value;
  if (n->op)
    capture_range(g, n, induction);
  else {
    constant(g, start);
    ins(g, 0xa5, R, 2);
    ref(g, 0x8d, induction, 0);
    ins(g, 0xa5, RH, 2);
    ref(g, 0x8d, induction, 1);
  }
  bind(g, head);
  ref(g, 0xad, induction, 0);
  ref(g, 0x8d, g->lhs, 0);
  ref(g, 0xad, induction, 1);
  ref(g, 0x8d, g->lhs, 1);
  if (n->op) {
    int negative = label(g), tested = label(g);
    ref(g, 0xad, induction, 2);
    ins(g, 0x85, R, 2);
    ref(g, 0xad, induction, 3);
    ins(g, 0x85, RH, 2);
    ref(g, 0xad, induction, 5);
    branch(g, 0x30, negative);
    compare(g, '<', 1);
    jump(g, tested);
    bind(g, negative);
    compare(g, '>', 1);
    bind(g, tested);
  } else {
    constant(g, stop);
    compare(g, step > 0 ? '<' : '>', 1);
  }
  ins(g, 0xa5, R, 2);
  branch(g, 0xf0, exhausted);
  ref(g, 0xad, induction, 0);
  ins(g, 0x85, R, 2);
  ref(g, 0xad, induction, 1);
  ins(g, 0x85, RH, 2);
  if (n->op)
    guard_loop_value(g, g->c->symbols[n->symbol].type);
  store_symbol(g, n->symbol);
  g->breaks[g->loops] = end;
  g->continues[g->loops++] = cont;
  block(g, (int)n->value);
  --g->loops;
  bind(g, cont);
  ins(g, 0x18, 0, 1);
  ref(g, 0xad, induction, 0);
  if (n->op)
    ref(g, 0x6d, induction, 4);
  else
    ins(g, 0x69, (int)step & 255, 2);
  ref(g, 0x8d, induction, 0);
  ref(g, 0xad, induction, 1);
  if (n->op)
    ref(g, 0x6d, induction, 5);
  else
    ins(g, 0x69, (int)(step & 65535) >> 8, 2);
  /* Crossing either i16 endpoint necessarily passes the captured stop value. */
  branch(g, 0x70, exhausted);
  ref(g, 0x8d, induction, 1);
  jump(g, head);
  bind(g, exhausted);
  block(g, n->else_body);
  bind(g, end);
}
static void each_loop(Backend *g, int i) {
  Node *n = &g->c->nodes[i], *source = &g->c->nodes[n->b];
  int storage = g->for_labels[i], head = label(g), cont = label(g),
      exhausted = label(g), end = label(g), string = source->type == TY_STR;
  int width = string ? 1 : type_size(n->type);
  if (string) {
    expr(g,
         n->b); /* Snapshot the original string even if its name is rebound. */
    ins(g, 0xa5, R, 2);
    ref(g, 0x8d, storage, 0);
    ins(g, 0xa5, RH, 2);
    ref(g, 0x8d, storage, 1);
    if (!g->characters)
      g->characters = label(g);
  } else if (source->type == TY_BYTES) {
    int k;
    expr(g, n->b);
    for (k = 0; k < 2; ++k) {
      ins(g, 0xa5, R + k, 2);
      ref(g, 0x8d, storage, k);
      ins(g, 0x85, P + k, 2);
    }
    length_call(g);
    for (k = 0; k < 2; ++k) {
      ins(g, 0xa5, R + k, 2);
      ref(g, 0x8d, storage, k + 2);
    }
  } else {
    int count, data;
    Symbol *s = source->kind == N_NAME ? &g->c->symbols[source->symbol] : NULL;
    if (s && s->reference) {
      int k;
      for (k = 0; k < 4; ++k) {
        ref(g, 0xad, s->label, k);
        ref(g, 0x8d, storage, k);
      }
    } else {
      if (s) {
        data = s->label;
        count = s->count;
      } else {
        count = source->d;
        if (source->kind == N_LIST)
          data = snapshot_list(g, n->b);
        else {
          Symbol temporary = {0};
          data = g->list_labels[n->b] = label(g);
          temporary.type = TY_ARRAY;
          temporary.element = source->c;
          temporary.count = count;
          temporary.label = data;
          zero_symbol(g, &temporary);
        }
      }
      set_pointer(g, data, 0);
      ins(g, 0xa5, P, 2);
      ref(g, 0x8d, storage, 0);
      ins(g, 0xa5, PH, 2);
      ref(g, 0x8d, storage, 1);
      constant(g, count);
      ins(g, 0xa5, R, 2);
      ref(g, 0x8d, storage, 2);
      ins(g, 0xa5, RH, 2);
      ref(g, 0x8d, storage, 3);
    }
  }
  bind(g, head);
  if (!string) {
    ref(g, 0xad, storage, 2);
    ref(g, 0x0d, storage, 3);
    branch(g, 0xf0, exhausted);
  }
  ref(g, 0xad, storage, 0);
  ins(g, 0x85, P, 2);
  ref(g, 0xad, storage, 1);
  ins(g, 0x85, PH, 2);
  ins(g, 0xa0, 0, 2);
  ins(g, 0xb1, P, 2);
  if (string) {
    branch(g, 0xf0, exhausted);
    character(g);
  } else {
    if (n->type == TY_FLOAT) {
      ins(g, 0xa5, P, 2);
      ins(g, 0x85, R, 2);
      ins(g, 0xa5, PH, 2);
      ins(g, 0x85, RH, 2);
    } else {
      ins(g, 0x85, R, 2);
      if (width == 2) {
        ins(g, 0xc8, 0, 1);
        ins(g, 0xb1, P, 2);
        ins(g, 0x85, RH, 2);
      } else
        normalize(g, n->type);
    }
  }
  if (g->c->symbols[n->symbol].type == TY_FLOAT && n->type != TY_FLOAT) {
    float_fac(g, n->type);
    float_finish(g);
  }
  store_symbol(g, n->symbol);
  g->breaks[g->loops] = end;
  g->continues[g->loops++] = cont;
  block(g, (int)n->value);
  --g->loops;
  bind(g, cont);
  ins(g, 0x18, 0, 1);
  ref(g, 0xad, storage, 0);
  ins(g, 0x69, width, 2);
  ref(g, 0x8d, storage, 0);
  ref(g, 0xad, storage, 1);
  ins(g, 0x69, 0, 2);
  ref(g, 0x8d, storage, 1);
  if (!string) {
    ins(g, 0x38, 0, 1);
    ref(g, 0xad, storage, 2);
    ins(g, 0xe9, 1, 2);
    ref(g, 0x8d, storage, 2);
    ref(g, 0xad, storage, 3);
    ins(g, 0xe9, 0, 2);
    ref(g, 0x8d, storage, 3);
  }
  jump(g, head);
  bind(g, exhausted);
  block(g, n->else_body);
  bind(g, end);
}
static void block(Backend *g, int i) {
  for (; i; i = g->c->nodes[i].next) {
    Node *n = &g->c->nodes[i];
    int head, end, otherwise;
    if (g->c->listing)
      fprintf(g->c->listing, "; source %s:%d\n",
              g->c->modules[g->c->tokens[n->token].module].path,
              g->c->tokens[n->token].line);
    switch (n->kind) {
    case N_ASSIGN:
      assignment(g, n);
      break;
    case N_EXPR:
      expr(g, n->a);
      break;
    case N_ASSERT:
      expr(g, n->a);
      truth(g, g->c->nodes[n->a].type);
      if (!n->b)
        branch(g, 0xf0, g->trap);
      else {
        end = label(g);
        branch(g, 0xd0, end);
        expr(g, n->b);
        if (!g->print_string)
          g->print_string = label(g);
        ref(g, 0x20, g->print_string, 0);
        output_character(g, 13);
        jump(g, g->trap);
        bind(g, end);
      }
      break;
    case N_IF:
      otherwise = label(g);
      end = label(g);
      expr(g, n->a);
      truth(g, g->c->nodes[n->a].type);
      branch(g, 0xf0, otherwise);
      block(g, n->b);
      jump(g, end);
      bind(g, otherwise);
      block(g, n->c);
      bind(g, end);
      break;
    case N_WHILE:
      head = label(g);
      otherwise = label(g);
      end = label(g);
      bind(g, head);
      expr(g, n->a);
      truth(g, g->c->nodes[n->a].type);
      branch(g, 0xf0, otherwise);
      g->breaks[g->loops] = end;
      g->continues[g->loops++] = head;
      block(g, n->b);
      --g->loops;
      jump(g, head);
      bind(g, otherwise);
      block(g, n->else_body);
      bind(g, end);
      break;
    case N_FOR:
      for_loop(g, i);
      break;
    case N_EACH:
      each_loop(g, i);
      break;
    case N_BREAK:
      jump(g, g->breaks[g->loops - 1]);
      break;
    case N_CONTINUE:
      jump(g, g->continues[g->loops - 1]);
      break;
    case N_RETURN:
      if (n->a)
        expr(g, n->a);
      ins(g, 0x60, 0, 1);
      break;
    default:
      break;
    }
  }
}
static void zero_symbol(Backend *g, Symbol *s) {
  if (s->parent)
    return;
  if (s->type == TY_STR || s->type == TY_BYTES) {
    if (s->type == TY_BYTES && !g->bytes_label)
      g->bytes_label = label(g);
    set_pointer(g, s->type == TY_BYTES ? g->bytes_label : g->string_label, 2);
    ins(g, 0xa5, P, 2);
    ref(g, 0x8d, s->label, 0);
    ins(g, 0xa5, PH, 2);
    ref(g, 0x8d, s->label, 1);
    return;
  }
  int n = symbol_size(s), k;
  for (k = 0; k < n; ++k) {
    if (k % 256 == 0) {
      int loop = label(g), chunk = n - k > 256 ? 256 : n - k;
      ins(g, 0xa9, 0, 2);
      ins(g, 0xa2, 0, 2);
      bind(g, loop);
      ref(g, 0x9d, s->label, k);
      ins(g, 0xe8, 0, 1);
      if (chunk == 256)
        branch(g, 0xd0, loop);
      else {
        ins(g, 0xe0, chunk, 2);
        branch(g, 0xd0, loop);
      }
    }
  }
}
static int live_symbol(Compiler *c, Symbol *s) {
  return !s->parent && !s->constant &&
         (s->scope == 0 || c->functions[s->scope - 1].used);
}
static void collect_loop_labels(Backend *g, int i) {
  for (; i; i = g->c->nodes[i].next) {
    Node *n = &g->c->nodes[i];
    if (n->kind == N_FOR || n->kind == N_EACH) {
      g->for_labels[i] = label(g);
      if (g->in_irq_function)
        g->irq_temps[i] = 1;
      collect_loop_labels(g, (int)n->value);
    } else if (n->kind == N_IF || n->kind == N_WHILE) {
      collect_loop_labels(g, n->b);
      collect_loop_labels(g, n->c);
    }
    collect_loop_labels(g, n->else_body);
  }
}
static int has_feature(Compiler *c, int i, int floating) {
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    if (n->kind == N_FUNC || n->kind == N_CLASS || n->kind == N_ENUMDEF ||
        n->kind == N_DECLCONST)
      continue;
    if ((floating == 3 && n->kind == N_CALL &&
         ((n->symbol <= -BI_IRQ_INSTALL && n->symbol >= -BI_IRQ_ACTIVE) ||
          (n->symbol <= -BI_MUSIC_START && n->symbol >= -BI_MUSIC_TICKS))) ||
        (floating == 4 && n->kind == N_CALL && n->symbol <= -BI_MUSIC_START &&
         n->symbol >= -BI_MUSIC_TICKS) ||
        (floating == 2 &&
         ((n->kind == N_CALL && n->symbol == -BI_BYTES && n->op) ||
          (n->kind == N_SLICE) ||
          (n->kind == N_BINARY && n->type == TY_BYTES &&
           (n->op == '+' || n->op == '*')))) ||
        (floating == 1 &&
         (n->type == TY_FLOAT || (n->kind == N_CALL && n->symbol == -BI_INT &&
                                  n->b && c->nodes[n->b].type == TY_STR))) ||
        (!floating && n->kind == N_CALL && n->symbol == -BI_INPUT))
      return 1;
    if (has_feature(c, n->a, floating) || has_feature(c, n->b, floating))
      return 1;
    if (n->kind == N_SLICE &&
        (has_feature(c, n->c, floating) || has_feature(c, n->d, floating)))
      return 1;
    if ((n->kind == N_IF && has_feature(c, n->c, floating)) ||
        ((n->kind == N_FOR || n->kind == N_EACH) &&
         (has_feature(c, n->c, floating) || has_feature(c, n->d, floating) ||
          has_feature(c, (int)n->value, floating))) ||
        has_feature(c, n->else_body, floating))
      return 1;
  }
  return 0;
}
static void collect_vic_literals(Backend *g, int i) {
  Compiler *c = g->c;
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    if (n->kind == N_FUNC || n->kind == N_CLASS || n->kind == N_ENUMDEF ||
        n->kind == N_DECLCONST)
      continue;
    if (n->kind == N_BYTES) {
      int asset;
      for (asset = 0; asset < c->nassets; ++asset)
        if (c->assets[asset].mode == AS_MUSIC &&
            c->assets[asset].offset == n->value) {
          SidInfo info;
          char error[256];
          sid_info(c->bytes + n->value, c->assets[asset].count, &info, error,
                   sizeof(error));
          byte_literal_label(g, (int)n->value);
          if (info.rsid)
            g->rsid_used = 1;
          if (info.load < 0xa000 && info.load + info.length > g->music_end)
            g->music_end = info.load + info.length;
        }
      if (c->byte_vic[n->value])
        byte_literal_label(g, (int)n->value);
      continue;
    }
    if (n->kind == N_CONST || n->kind == N_STRING || n->kind == N_FLOAT ||
        n->kind == N_NAME || n->kind == N_FIELD || n->kind == N_ATTR ||
        n->kind == N_HW || n->kind == N_PARAM || n->kind == N_NONE)
      continue;
    if (n->kind == N_CONSTRUCT && object_type(n->type)) {
      ClassLayout *layout = &c->classes[n->type - TY_CLASS_BASE];
      int field;
      for (field = 0; field < layout->nfields; ++field)
        collect_vic_literals(g, c->nodes[layout->fields[field]].b);
    }
    collect_vic_literals(g, n->a);
    collect_vic_literals(g, n->b);
    if (n->kind == N_IF || n->kind == N_FOR || n->kind == N_EACH ||
        n->kind == N_SLICE)
      collect_vic_literals(g, n->c);
    if (n->kind == N_FOR || n->kind == N_EACH || n->kind == N_SLICE)
      collect_vic_literals(g, n->d);
    if (n->kind == N_FOR || n->kind == N_EACH)
      collect_vic_literals(g, (int)n->value);
    collect_vic_literals(g, n->else_body);
  }
}
static void emit_byte_record(Backend *g, int i) {
  Compiler *c = g->c;
  int count = c->bytes[i] | (c->bytes[i + 1] << 8), offset = i + 2;
  int alignment = c->byte_alignment[offset], payload = pc(g) + 2;
  int j, has_asset = 0;
  if (alignment) {
    payload = (payload + alignment - 1) & ~(alignment - 1);
    if (c->byte_vic[offset] && payload < 0x2000 && payload + count > 0x1000)
      payload = 0x2000;
    /* Music executes at its original addresses. Avoid its reserved RAM even
       when a VIC asset would otherwise fit there. */
    for (j = 0; j < c->nassets; ++j)
      if (c->assets[j].mode == AS_MUSIC &&
          g->bytes_labels[c->assets[j].offset]) {
        SidInfo info;
        char error[256];
        sid_info(c->bytes + c->assets[j].offset, c->assets[j].count, &info,
                 error, sizeof(error));
        if (payload - 2 < info.load + info.length &&
            payload + count > info.load) {
          payload =
              (info.load + info.length + 2 + alignment - 1) & ~(alignment - 1);
          if (c->byte_vic[offset] && payload < 0x2000 &&
              payload + count > 0x1000)
            payload = 0x2000;
          j = -1; /* An advance can encounter another reserved range. */
        }
      }
    if (c->byte_vic[offset] && payload < 0x2000 && payload + count > 0x1000)
      payload = 0x2000;
    if (c->byte_vic[offset] && payload + count > 0x4000)
      fail(c, 0,
           "VIC assets exceed visible RAM in bank 0 "
           "($0800..$0FFF/$2000..$3FFF)");
    while (pc(g) + 2 < payload)
      byte(g, 0);
  }
  bind(g, byte_literal_label(g, offset));
  for (j = 0; j < count + 2; ++j)
    byte(g, c->bytes[i + j]);
  for (j = 0; j < c->nassets; ++j)
    if (c->assets[j].offset == offset) {
      Asset *asset = &c->assets[j];
      has_asset = 1;
      if (c->map)
        fprintf(c->map, "asset $%04X %d align=%d mode=%d %s\n", payload, count,
                alignment ? alignment : 1, asset->mode, asset->path);
    }
  if (has_asset)
    c->asset_size += count;
}
void generate(Compiler *c) {
  Backend *g = (Backend *)calloc(1, sizeof(Backend));
  int i, j, k;
  static const unsigned char basic[] = {0x0b, 0x08, 10,  0, 0x9e, '2',
                                        '0',  '6',  '1', 0, 0,    0};
  if (!g)
    fail(c, 0, "unable to acquire backend arena");
  g->c = c;
  c->asset_size = 0;
  g->lhs = label(g);
  g->math = label(g);
  g->string_label = label(g);
  g->trap = label(g);
  g->exit_label = label(g);
  j = has_feature(c, c->root, 3);
  for (i = 0; !j && i < c->nfunctions; ++i)
    if (c->functions[i].used)
      j = has_feature(c, c->nodes[c->functions[i].node].b, 3);
  if (j) {
    g->irq_work = label(g);
    g->irq_hook = label(g);
    g->irq_stop = label(g);
    g->irq_install = label(g);
    g->irq_line = label(g);
    g->irq_dispatch = label(g);
    g->irq_backup = label(g);
  }
  j = has_feature(c, c->root, 4);
  for (i = 0; !j && i < c->nfunctions; ++i)
    if (c->functions[i].used)
      j = has_feature(c, c->nodes[c->functions[i].node].b, 4);
  if (j) {
    g->music_work = label(g);
    g->music_start = label(g);
    g->music_stop = label(g);
    g->music_tick = label(g);
    g->music_invoke = label(g);
    g->music_silence = label(g);
    g->music_empty = label(g);
    g->music_zp = label(g);
    g->music_foreground = label(g);
    g->music_cia_prepare = label(g);
    g->music_cia_restore = label(g);
    g->music_cia_save = label(g);
  }
  j = has_feature(c, c->root, 0);
  for (i = 0; !j && i < c->nfunctions; ++i)
    if (c->functions[i].used)
      j = has_feature(c, c->nodes[c->functions[i].node].b, 0);
  if (j) {
    g->input_line = label(g);
    g->input_work = label(g);
    g->input_buffer = label(g);
    g->input_pool = label(g);
  }
  j = has_feature(c, c->root, 1);
  for (i = 0; !j && i < c->nfunctions; ++i)
    if (c->functions[i].used)
      j = has_feature(c, c->nodes[c->functions[i].node].b, 1);
  if (j) {
    g->float_result = label(g);
    g->float_left = label(g);
    g->float_state = label(g);
    g->float_text = label(g);
    g->float_65536 = label(g);
  }
  j = has_feature(c, c->root, 2);
  for (i = 0; !j && i < c->nfunctions; ++i)
    if (c->functions[i].used)
      j = has_feature(c, c->nodes[c->functions[i].node].b, 2);
  if (j) {
    g->bytes_allocate = label(g);
    g->bytes_pool = label(g);
    g->bytes_used = label(g);
    bytes_workspace(g);
    g->bytes_label = label(g);
  }
  for (i = 1; i <= c->nnodes; ++i)
    if (c->nodes[i].kind == N_CALL && c->nodes[i].symbol <= -BI_FILE_OPEN &&
        c->nodes[i].symbol >= -BI_FILE_ERROR) {
      g->file_work = label(g);
      break;
    }
  for (i = 0; i < c->nsymbols; ++i)
    c->symbols[i].label = live_symbol(c, &c->symbols[i]) ? label(g) : 0;
  for (i = 0; i < c->nfunctions; ++i)
    c->functions[i].label = label(g);
  collect_loop_labels(g, c->root);
  for (i = 0; i < c->nfunctions; ++i)
    if (c->functions[i].used) {
      g->in_irq_function = c->functions[i].irq != 0;
      collect_loop_labels(g, c->nodes[c->functions[i].node].b);
    }
  g->in_irq_function = 0;
  for (i = 0; i < (int)sizeof(basic); ++i)
    byte(g, basic[i]);
  collect_vic_literals(g, c->root);
  for (i = 0; i < c->nfunctions; ++i)
    if (c->functions[i].used)
      collect_vic_literals(g, c->nodes[c->functions[i].node].b);
  if (g->music_work && g->rsid_used) {
    g->music_rsid_prepare = label(g);
    g->music_rsid_restore = label(g);
    g->music_rsid_invoke = label(g);
    g->music_rsid_resume = label(g);
  }
  j = g->music_end != 0;
  for (i = 0; i < c->nbytes; i += 2 + (c->bytes[i] | (c->bytes[i + 1] << 8)))
    if (g->bytes_labels[i + 2] && c->byte_vic[i + 2])
      j = 1;
  if (j) {
    int startup = label(g), before, bitmap = 0;
    jump(g, startup);
    before = c->image_size;
    for (i = 0; i < c->nbytes; i += 2 + (c->bytes[i] | (c->bytes[i + 1] << 8)))
      if (g->bytes_labels[i + 2] && c->byte_vic[i + 2] &&
          c->byte_alignment[i + 2] == 8192)
        bitmap = 1;
    /* Fill low RAM first. A bitmap reserves $2000..$3F3F, so defer records
       that need high RAM until its fixed block has been emitted. */
    if (bitmap) {
      for (k = 64; k < 8192; k *= 2)
        for (i = 0; i < c->nbytes;
             i += 2 + (c->bytes[i] | (c->bytes[i + 1] << 8)))
          if (g->bytes_labels[i + 2] && c->byte_vic[i + 2] &&
              c->byte_alignment[i + 2] == k &&
              ((pc(g) + 2 + k - 1) & ~(k - 1)) +
                      (c->bytes[i] | (c->bytes[i + 1] << 8)) <=
                  0x1000)
            emit_byte_record(g, i);
      for (i = 0; i < c->nbytes;
           i += 2 + (c->bytes[i] | (c->bytes[i + 1] << 8)))
        if (g->bytes_labels[i + 2] && c->byte_vic[i + 2] &&
            c->byte_alignment[i + 2] == 8192)
          emit_byte_record(g, i);
    }
    for (k = 64; k <= 8192; k *= 2)
      for (i = 0; i < c->nbytes;
           i += 2 + (c->bytes[i] | (c->bytes[i + 1] << 8)))
        if (g->bytes_labels[i + 2] && c->byte_vic[i + 2] &&
            c->byte_alignment[i + 2] == k &&
            g->labels[g->bytes_labels[i + 2]] < 0)
          emit_byte_record(g, i);
    while (pc(g) < g->music_end)
      byte(g, 0);
    g->prefix_data = c->image_size - before;
    bind(g, startup);
  }
  if (c->listing)
    fprintf(c->listing,
            "; PyC64 %s native 6502 listing (resolved bytes are in the PRG)\n; "
            "BASIC launcher: 10 SYS2061 at $0801\n",
            VERSION);
  if (c->listing || c->map) {
    for (i = 0; i < c->nmodules; ++i) {
      if (c->listing)
        fprintf(c->listing, "; module %s: %s\n", c->modules[i].name,
                c->modules[i].path);
      if (c->map)
        fprintf(c->map, "module %s %s\n", c->modules[i].name,
                c->modules[i].path);
    }
  }
  if (c->map)
    for (i = 0; i < c->nenums; ++i) {
      EnumLayout *e = &c->enums[i];
      const char *prefix = e->module ? c->modules[e->module].name : "";
      fprintf(c->map, "enum %s%s%s %s members=%d\n", prefix,
              e->module ? "." : "", e->name, type_name(e->type), e->count);
      for (j = e->first; j < e->first + e->count; ++j) {
        EnumMember *m = &c->enum_members[j];
        fprintf(c->map, "enum_member %s%s%s.%s %ld canonical=%s\n", prefix,
                e->module ? "." : "", e->name, m->name, m->value,
                c->enum_members[m->canonical].name);
      }
    }
  ins(g, 0xd8, 0, 1);
  if (g->music_work) {
    ins(g, 0xa9, 0, 2);
    for (i = 0; i < music_state_size(g); ++i)
      ref(g, 0x8d, g->music_work, i);
  }
  for (i = P; i <= RH; ++i) {
    ins(g, 0xa5, i, 2);
    ins(g, 0x48, 0, 1);
  }
  for (i = 0; i < c->nsymbols; ++i)
    if (c->symbols[i].scope == 0 && !c->symbols[i].constant)
      zero_symbol(g, &c->symbols[i]);
  if (g->file_work) {
    ins(g, 0xa9, 0, 2);
    for (i = 0; i < 5; ++i)
      ref(g, 0x8d, g->file_work, i);
  }
  if (g->input_work) {
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->input_work, 0);
    ref(g, 0x8d, g->input_work, 1);
  }
  if (g->bytes_used) {
    ins(g, 0xa9, 0, 2);
    ref(g, 0x8d, g->bytes_used, 0);
    ref(g, 0x8d, g->bytes_used, 1);
  }
  if (g->float_state) {
    static const unsigned char scan[] = {
        0xe6, 0x7a, 0xd0, 2,    0xe6, 0x7b, 0xad, 0,  0,    0xc9, 0x3a, 0xb0,
        10,   0xc9, 32,   0xf0, 0xef, 0x38, 0xe9, 48, 0x38, 0xe9, 0xd0, 0x60};
    int loop = label(g);
    ins(g, 0xa2, 0, 2);
    bind(g, loop);
    ins(g, 0xb5, 2, 2);
    ref(g, 0x9d, g->float_state, 0);
    ins(g, 0xe8, 0, 1);
    ins(g, 0xe0, 142, 2);
    branch(g, 0xd0, loop);
    for (i = 0; i < 2; ++i) {
      ins(g, 0xad, 0x0300 + i, 3);
      ref(g, 0x8d, g->float_state, 142 + i);
    }
    set_pointer(g, g->trap, 0);
    ins(g, 0xa5, P, 2);
    ins(g, 0x8d, 0x0300, 3);
    ins(g, 0xa5, PH, 2);
    ins(g, 0x8d, 0x0301, 3);
    for (i = 0; i < (int)sizeof(scan); ++i) {
      ins(g, 0xa9, scan[i], 2);
      ins(g, 0x85, 0x73 + i, 2);
    }
  }
  block(g, c->root);
  jump(g, g->exit_label);
  for (i = 0; i < c->nfunctions; ++i)
    if (c->functions[i].used) {
      Function *f = &c->functions[i];
      g->in_irq_function = f->irq != 0;
      bind(g, f->label);
      if (c->map)
        fprintf(c->map, "function %s%s%s $%04X\n",
                f->module ? c->modules[f->module].name : "",
                f->module ? "." : "", f->name, pc(g));
      for (j = 0; j < c->nsymbols; ++j)
        if (c->symbols[j].scope == i + 1) {
          int param = 0;
          for (k = 0; k < f->argc; ++k)
            if (f->param[k] == j)
              param = 1;
          if (!param)
            zero_symbol(g, &c->symbols[j]);
        }
      block(g, c->nodes[f->node].b);
      ins(g, 0x60, 0, 1);
    }
  g->in_irq_function = 0;
  emit_print_helpers(g);
  if ((g->print_bytes || g->bytes_compare || g->bytes_construct ||
       g->bytes_join || g->bytes_repeat || g->bytes_slice || g->bytes_search) &&
      !g->string_length)
    g->string_length = label(g);
  emit_string_helpers(g);
  emit_bytes_helpers(g);
  emit_input_helper(g);
  emit_float_helpers(g);
  emit_irq_helpers(g);
  emit_music_helpers(g);
  bind(g, g->exit_label);
  if (g->irq_work) {
    ins(g, 0x08, 0, 1);
    ins(g, 0x78, 0, 1);
    ref(g, 0x20, g->irq_stop, 0);
    ins(g, 0x28, 0, 1);
  }
  if (g->float_state) {
    int loop = label(g);
    ins(g, 0xa2, 0, 2);
    bind(g, loop);
    ref(g, 0xbd, g->float_state, 0);
    ins(g, 0x95, 2, 2);
    ins(g, 0xe8, 0, 1);
    ins(g, 0xe0, 142, 2);
    branch(g, 0xd0, loop);
    for (i = 0; i < 2; ++i) {
      ref(g, 0xad, g->float_state, 142 + i);
      ins(g, 0x8d, 0x0300 + i, 3);
    }
  }
  for (i = RH; i >= P; --i) {
    ins(g, 0x68, 0, 1);
    ins(g, 0x85, i, 2);
  }
  ins(g, 0x60, 0, 1);
  bind(g, g->trap);
  if (g->irq_work) {
    ins(g, 0x08, 0, 1);
    ins(g, 0x78, 0, 1);
    ref(g, 0x20, g->irq_stop, 0);
    ins(g, 0x28, 0, 1);
  }
  ins(g, 0x00, 0, 1);
  jump(g, g->trap);
  c->code_size = c->image_size - g->prefix_data;
  bind(g, g->lhs);
  byte(g, 0);
  byte(g, 0);
  bind(g, g->math);
  byte(g, 0);
  byte(g, 0);
  if (g->enum_work) {
    bind(g, g->enum_work);
    byte(g, 0);
    byte(g, 0);
  }
  if (g->power_work) {
    bind(g, g->power_work);
    for (i = 0; i < 6; ++i)
      byte(g, 0);
  }
  if (g->division_work) {
    bind(g, g->division_work);
    for (i = 0; i < 4; ++i)
      byte(g, 0);
  }
  if (g->file_work) {
    bind(g, g->file_work);
    for (i = 0; i < 5; ++i)
      byte(g, 0);
  }
  if (g->file_name) {
    bind(g, g->file_name);
    for (i = 0; i < 256; ++i)
      byte(g, 0);
  }
  if (g->string_work) {
    bind(g, g->string_work);
    for (i = 0; i < 6; ++i)
      byte(g, 0);
  }
  if (g->bytes_work) {
    bind(g, g->bytes_work);
    for (i = 0; i < (g->bytes_search                    ? 32
                     : g->bytes_slice                   ? 26
                     : g->bytes_join || g->bytes_repeat ? 20
                                                        : 12);
         ++i)
      byte(g, 0);
  }
  if (g->bytes_used) {
    bind(g, g->bytes_used);
    byte(g, 0);
    byte(g, 0);
    bind(g, g->bytes_pool);
    if (c->map)
      fprintf(c->map, "runtime immutable_bytes $%04X 4096\n", pc(g));
    for (i = 0; i < 4096; ++i)
      byte(g, 0);
  }
  if (g->input_work) {
    bind(g, g->input_work);
    for (i = 0; i < 8; ++i)
      byte(g, 0);
    bind(g, g->input_buffer);
    for (i = 0; i < 256; ++i)
      byte(g, 0);
    bind(g, g->input_pool);
    if (c->map)
      fprintf(c->map, "runtime input_strings $%04X 4096\n", pc(g));
    for (i = 0; i < 4096; ++i)
      byte(g, 0);
  }
  if (g->arguments) {
    bind(g, g->arguments);
    for (i = 0; i < g->argument_count * 2; ++i)
      byte(g, 0);
  }
  if (g->reu_flags) {
    bind(g, g->reu_flags);
    byte(g, 0);
  }
  if (g->irq_work) {
    if (pc(g) & 1)
      byte(g, 0); /* Both indirect JMP words avoid the NMOS page-wrap bug. */
    bind(g, g->irq_work);
    if (c->map)
      fprintf(c->map, "runtime raster_irq $%04X %d\n", pc(g),
              g->music_work ? 10 : 9);
    for (i = 0; i < (g->music_work ? 10 : 9); ++i)
      byte(g, 0);
    bind(g, g->irq_backup);
    if (c->map)
      fprintf(c->map, "runtime irq_backup $%04X %d\n", pc(g), g->irq_size);
    for (i = 0; i < g->irq_size; ++i)
      byte(g, 0);
  }
  if (g->music_work) {
    if (pc(g) & 1)
      byte(g, 0);
    bind(g, g->music_work);
    if (c->map)
      fprintf(c->map, "runtime music $%04X %d\n", pc(g), music_state_size(g));
    for (i = 0; i < music_state_size(g); ++i)
      byte(g, 0);
    bind(g, g->music_zp);
    for (i = 0; i < 254; ++i)
      byte(g, 0);
    bind(g, g->music_foreground);
    for (i = 0; i < 256; ++i)
      byte(g, 0);
  }
  if (g->float_state) {
    unsigned char packed[5];
    bind(g, g->float_result);
    for (i = 0; i < 5; ++i)
      byte(g, 0);
    bind(g, g->float_left);
    for (i = 0; i < 5; ++i)
      byte(g, 0);
    bind(g, g->float_state);
    for (i = 0; i < 160; ++i)
      byte(g, 0);
    bind(g, g->float_text);
    for (i = 0; i < 256; ++i)
      byte(g, 0);
    bind(g, g->float_65536);
    float_pack(c, 0, 65536, packed);
    for (i = 0; i < 5; ++i)
      byte(g, packed[i]);
    if (g->float_arguments) {
      bind(g, g->float_arguments);
      for (i = 0; i < g->float_argument_count * 5; ++i)
        byte(g, 0);
    }
  }
  if (g->float_integer_work) {
    bind(g, g->float_integer_work);
    for (i = 0; i < 6; ++i)
      byte(g, 0);
  }
  if (g->float_divmod_work) {
    bind(g, g->float_divmod_work);
    if (c->map)
      fprintf(c->map, "runtime float_divmod $%04X 36\n", pc(g));
    for (i = 0; i < 36; ++i)
      byte(g, 0);
  }
  if (g->print_work) {
    bind(g, g->print_work);
    byte(g, 0);
    byte(g, 0);
  }
  if (g->sprite_masks) {
    bind(g, g->sprite_masks);
    for (i = 0; i < 8; ++i)
      byte(g, 1 << i);
  }
  for (i = 0; i < c->nsymbols; ++i) {
    Symbol *s = &c->symbols[i];
    if (s->constant && c->map) {
      if (s->type == TY_STR || s->type == TY_BYTES)
        fprintf(c->map, "constant %s%s%s %s length=%d\n",
                s->module ? c->modules[s->module].name : "",
                s->module ? "." : "", s->name, type_name(s->type), s->count);
      else if (s->type == TY_FLOAT)
        fprintf(c->map, "constant %s%s%s float %.17g\n",
                s->module ? c->modules[s->module].name : "",
                s->module ? "." : "", s->name, s->real);
      else
        fprintf(c->map, "constant %s%s%s %s %ld\n",
                s->module ? c->modules[s->module].name : "",
                s->module ? "." : "", s->name, type_name(s->type), s->value);
    }
    if (!live_symbol(c, s))
      continue;
    int size = symbol_size(s);
    bind(g, s->label);
    if (c->map)
      fprintf(c->map, "variable %s%s%s%s%s $%04X %d %s\n",
              s->module ? c->modules[s->module].name : "", s->module ? "." : "",
              s->scope ? c->functions[s->scope - 1].name : "",
              s->scope ? "." : "", s->name, pc(g), size, type_name(s->type));
    for (j = 0; j < size; ++j)
      byte(g, 0);
  }
  if (c->map)
    for (i = 0; i < c->nsymbols; ++i) {
      Symbol *field = &c->symbols[i];
      if (!field->parent)
        continue;
      Symbol *object = &c->symbols[field->parent - 1];
      if (!live_symbol(c, object))
        continue;
      if (object->reference)
        fprintf(c->map, "field %s%s%s.%s offset=%ld %d %s\n",
                field->module ? c->modules[field->module].name : "",
                field->module ? "." : "", c->functions[field->scope - 1].name,
                field->name, field->value, type_size(field->type),
                type_name(field->type));
      else
        fprintf(c->map, "variable %s%s%s%s%s $%04X %d %s\n",
                field->module ? c->modules[field->module].name : "",
                field->module ? "." : "",
                field->scope ? c->functions[field->scope - 1].name : "",
                field->scope ? "." : "", field->name,
                g->labels[object->label] + (int)field->value,
                type_size(field->type), type_name(field->type));
    }
  for (i = 1; i <= c->nnodes; ++i)
    if (g->list_labels[i]) {
      bind(g, g->list_labels[i]);
      for (j = 0; j < c->nodes[i].d * type_size(c->nodes[i].c); ++j)
        byte(g, 0);
    }
  for (i = 1; i <= c->nnodes; ++i)
    if (g->for_labels[i]) {
      Node *n = &c->nodes[i];
      int size = n->kind == N_EACH ? (c->nodes[n->b].type == TY_STR ? 2 : 4)
                                   : (n->op ? 6 : 2);
      bind(g, g->for_labels[i]);
      for (j = 0; j < size; ++j)
        byte(g, 0);
    }
  if (g->characters) {
    bind(g, g->characters);
    for (i = 0; i < 128; ++i) {
      byte(g, i != 0);
      byte(g, 0);
      byte(g, i);
      byte(g, 0);
    }
  }
  for (i = 1; i <= c->nnodes; ++i)
    if (g->float_literals[i]) {
      unsigned char packed[5];
      bind(g, g->float_literals[i]);
      float_pack(c, c->nodes[i].token, c->nodes[i].real, packed);
      for (j = 0; j < 5; ++j)
        byte(g, packed[j]);
    }
  for (i = 0; i < c->nstrings;) {
    int length = (int)strlen(c->strings + i);
    if (g->string_labels[i]) {
      bind(g, g->string_labels[i]);
      byte(g, length & 255);
      byte(g, length >> 8);
      for (j = 0; j <= length; ++j)
        byte(g, (unsigned char)c->strings[i + j]);
    }
    i += length + 1;
  }
  bind(g, g->string_label);
  byte(g, 0);
  byte(g, 0);
  byte(g, 0); /* Shared default empty string. */
  if (g->bytes_label)
    for (i = 0; i < c->nbytes;) {
      int count = c->bytes[i] | (c->bytes[i + 1] << 8);
      int offset = i + 2, record = g->bytes_labels[offset];
      if ((i == 0 || record) && (!record || g->labels[record] < 0))
        emit_byte_record(g, i);
      i += count + 2;
    }
  c->data_size = c->image_size - c->code_size;
  for (i = 0; i < g->nfixups; ++i) {
    Fixup *f = &g->fixups[i];
    int addr;
    if (f->label < 0) {
      addr = g->labels[-f->label] + (f->addend & 65535);
      c->image[f->offset] =
          (unsigned char)((f->addend & 0x10000) ? addr >> 8 : addr);
    } else {
      if (g->labels[f->label] < 0)
        fail(c, 0, "internal error: unresolved native label");
      addr = g->labels[f->label] + f->addend;
      c->image[f->offset] = (unsigned char)addr;
      c->image[f->offset + 1] = (unsigned char)(addr >> 8);
    }
  }
  if (c->map)
    fprintf(c->map, "layout $0801 $%04X code=%d data=%d total=%d\n", pc(g) - 1,
            c->code_size, c->data_size, c->image_size);
  free(g);
}

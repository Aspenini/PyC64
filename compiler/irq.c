#include "pyc64.h"

/* A callback selector names a source function; it is not a runtime value. */
int irq_handler(Compiler *c, int reference) {
  int module, i;
  char name[192], candidate[192];
  if (!reference || !named_expression(c, reference))
    fail(c, c->nodes[reference].token,
         "IRQ callback must name a static zero-argument function");
  resolve_name(c, reference, &module, name, sizeof(name));
  for (i = c->root; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    if (n->kind != N_FUNC || c->tokens[n->token].module != module)
      continue;
    function_name(c, i, candidate, sizeof(candidate));
    if (strcmp(candidate, name))
      continue;
    if (n->a || object_type(n->c))
      fail(c, c->nodes[reference].token,
           "IRQ callback requires a plain zero-argument function");
    if (n->type && n->type != TY_VOID)
      fail(c, c->nodes[reference].token, "IRQ callback must return None");
    return i;
  }
  fail(c, c->nodes[reference].token,
       "IRQ callback must name a source function");
  return 0;
}
int irq_registration(Compiler *c, int node) {
  Node *n = &c->nodes[node];
  int module, first, second;
  char name[192];
  if (n->kind != N_CALL || !named_expression(c, n->a))
    return 0;
  resolve_name(c, n->a, &module, name, sizeof(name));
  if (strcmp(name, "c64.irq.on_raster"))
    return 0;
  first = n->b;
  second = c->nodes[first].next;
  if (!first || !second || c->nodes[second].next)
    fail(c, n->token, "on_raster expects a line and a callback function");
  return irq_handler(c, second);
}
static void irq_block(Compiler *c, int i);
static void irq_function(Compiler *c, int f) {
  if (c->functions[f].irq == 2)
    return;
  c->functions[f].irq = 2;
  irq_block(c, c->nodes[c->functions[f].node].b);
}
static void irq_block(Compiler *c, int i) {
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    if (n->type == TY_FLOAT || n->kind == N_FLOAT || n->kind == N_FLOAT_CAST)
      fail(c, n->token,
           "IRQ callbacks cannot use BASIC ROM floating-point operations");
    if (n->kind == N_NAME || n->kind == N_FIELD || n->kind == N_ATTR ||
        n->kind == N_CONST || n->kind == N_STRING || n->kind == N_BYTES ||
        n->kind == N_HW || n->kind == N_PARAM || n->kind == N_NONE)
      continue;
    if (n->kind == N_SLICE || (n->kind == N_BINARY && n->type == TY_BYTES &&
                               (n->op == '+' || n->op == '*')))
      fail(c, n->token, "IRQ callbacks cannot allocate immutable bytes");
    if (n->kind == N_ASSERT && n->b)
      fail(c, n->token, "IRQ assertions cannot print failure messages");
    if (n->kind == N_CALL || n->kind == N_PROPERTY_CALL ||
        n->kind == N_CONSTRUCT) {
      int builtin = n->kind == N_CALL ? -n->symbol : 0;
      if (n->symbol >= 0)
        irq_function(c, n->symbol);
      else if (builtin == BI_PRINT || builtin == BI_INPUT ||
               builtin == BI_KEY_READ ||
               (builtin >= BI_FILE_OPEN && builtin <= BI_FILE_ERROR) ||
               (builtin >= BI_MATH_POW && builtin <= BI_MATH_TRUNC) ||
               (builtin == BI_INT && n->b &&
                (c->nodes[n->b].type == TY_STR ||
                 c->nodes[n->b].type == TY_FLOAT)) ||
               builtin == BI_CAST_BASE + TY_FLOAT)
        fail(c, n->token,
             "IRQ callbacks cannot call blocking or ROM-dependent functions");
      else if (builtin == BI_BYTES)
        fail(c, n->token, "IRQ callbacks cannot construct runtime bytes");
      else if (builtin == BI_IRQ_INSTALL || builtin == BI_MUSIC_START)
        fail(c, n->token, "register IRQ callbacks from foreground code");
      if (n->kind == N_CONSTRUCT && object_type(n->type)) {
        ClassLayout *layout = &c->classes[n->type - TY_CLASS_BASE];
        int field;
        for (field = 0; field < layout->nfields; ++field)
          if (c->nodes[layout->fields[field]].type == TY_FLOAT)
            fail(c, n->token,
                 "IRQ callbacks cannot construct classes with float fields");
      }
      irq_block(c, n->b);
    } else if (n->kind == N_FOR || n->kind == N_EACH) {
      irq_block(c, n->b);
      irq_block(c, n->c);
      irq_block(c, n->d);
      irq_block(c, (int)n->value);
      irq_block(c, n->else_body);
    } else {
      irq_block(c, n->a);
      irq_block(c, n->b);
      if (n->kind == N_IF)
        irq_block(c, n->c);
      if (n->kind == N_WHILE)
        irq_block(c, n->else_body);
    }
  }
}
void validate_irq(Compiler *c) {
  int i;
  for (i = 0; i < c->nfunctions; ++i)
    if (c->functions[i].irq)
      irq_function(c, i);
}

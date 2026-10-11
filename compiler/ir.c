#include "pyc64.h"

/* The compact tree IR reuses arena node indices after semantic resolution.
   Names have symbol indices, calls have static targets, hardware has addresses,
   and every value has a concrete type. Bound the hardware stack before
   emission. */
static int maximum(int a, int b) { return a > b ? a : b; }
static int expression_cost(Compiler *c, int i, int *cache);
static int block_cost(Compiler *c, int i, int *cache);
static int function_cost(Compiler *c, int f, int *cache) {
  if (c->nodes[c->functions[f].node].op)
    return 0;
  if (cache[f] < 0)
    cache[f] = block_cost(c, c->nodes[c->functions[f].node].b, cache);
  return cache[f];
}
static int expression_cost(Compiler *c, int i, int *cache) {
  Node *n;
  int a, b, result = 0, bytes = 0, arg;
  if (!i)
    return 0;
  n = &c->nodes[i];
  switch (n->kind) {
  case N_BINARY:
    a = expression_cost(c, n->a, cache);
    b = expression_cost(c, n->b, cache);
    result = maximum(a, b + (n->op == OP_AND || n->op == OP_OR ? 0
                             : c->nodes[n->a].type == TY_FLOAT ? 5
                                                               : 2));
    if (c->nodes[n->a].type == TY_FLOAT && n->op != OP_AND && n->op != OP_OR)
      result = maximum(
          result,
          n->op == T_POWER || n->op == T_FLOORDIV || n->op == '%' ? 48 : 32);
    if ((c->nodes[n->a].type == TY_STR || c->nodes[n->a].type == TY_BYTES) &&
        n->op != OP_AND && n->op != OP_OR)
      result =
          maximum(result, 4); /* Comparison helper and header-length JSR. */
    if (n->type == TY_BYTES && (n->op == '+' || n->op == '*'))
      result = maximum(result,
                       4); /* Allocate/copy helpers, including count * bytes. */
    if (n->op == OP_IN || n->op == OP_NOTIN)
      result = maximum(result, 4); /* Search helper and nested byte matcher. */
    break;
  case N_UNARY:
  case N_ENUM_VALUE:
    result = expression_cost(c, n->a, cache);
    break;
  case N_ENUM_LOOKUP:
    result = maximum(2, expression_cost(c, n->a, cache));
    break;
  case N_FLOAT_CAST:
    result = maximum(32, expression_cost(c, n->a, cache));
    break;
  case N_BYTES_SEARCH:
    result = maximum(8, expression_cost(c, n->a, cache));
    bytes = 2;
    for (arg = n->b; arg; arg = c->nodes[arg].next) {
      result = maximum(result, bytes + expression_cost(c, arg, cache));
      bytes += 2;
    }
    break;
  case N_SLICE: {
    int parts[4] = {n->a, n->b, n->c, n->d}, k;
    result = 8; /* Capture source and all bounds before shared helper work. */
    for (k = 0; k < 4; ++k)
      result = maximum(result, k * 2 + expression_cost(c, parts[k], cache));
    break;
  }
  case N_INDEX:
    result = maximum(
        expression_cost(c, n->a, cache),
        expression_cost(c, n->b, cache) +
            (c->nodes[n->a].type == TY_STR || c->nodes[n->a].type == TY_BYTES
                 ? 2
                 : 0));
    if (c->nodes[n->a].type == TY_STR || c->nodes[n->a].type == TY_BYTES)
      result = maximum(result, 2); /* Length helper after popping the source. */
    break;
  case N_CALL:
  case N_CONSTRUCT:
  case N_PROPERTY_CALL:
    for (arg = n->b; arg; arg = c->nodes[arg].next) {
      result = maximum(result, bytes + expression_cost(c, arg, cache));
      bytes += c->nodes[arg].type == TY_ARRAY || c->nodes[arg].type == TY_LIST
                   ? 4
               : c->nodes[arg].type == TY_FLOAT ? 5
                                                : 2;
    }
    result = maximum(result, bytes);
    if (n->symbol == -BI_PRINT)
      result = maximum(result, 4); /* Formatter JSR and KERNAL CHROUT JSR. */
    if (n->symbol == -BI_KEY_READ)
      result = maximum(result, 2); /* KERNAL GETIN JSR. */
    if (n->symbol == -BI_INPUT)
      result = maximum(result, 12); /* Line helper plus KERNAL stack margin. */
    if (n->symbol == -BI_BYTES)
      result = maximum(result, 4); /* Constructor/allocation helper stack. */
    if (n->symbol == -BI_REU_PRESENT)
      result = maximum(result, 3); /* Saved flags and two address registers. */
    if (n->symbol <= -BI_IRQ_INSTALL && n->symbol >= -BI_IRQ_RESTORE)
      result =
          maximum(result, 5); /* Line capture, flags, installer/stop JSR. */
    if (n->symbol == -BI_REU_TRANSFER)
      result =
          maximum(result, 13); /* Six captured words plus PHP before popping. */
    if (n->symbol >= -BI_MUSIC_TICKS && n->symbol <= -BI_MUSIC_START)
      result = maximum(result, 12); /* Init/stop trampoline and saved flags. */
    if (n->symbol == -(BI_CAST_BASE + TY_FLOAT))
      result = maximum(result, 32);
    if (n->symbol <= -BI_MATH_POW && n->symbol >= -BI_MATH_TRUNC)
      result =
          maximum(result, 48); /* Transcendental ROM routines nest calls. */
    if (n->symbol == -BI_INT && n->b && c->nodes[n->b].type == TY_STR)
      result = maximum(result, 32);
    if (n->symbol == -BI_INT || n->symbol < -BI_CAST_BASE)
      result = maximum(result, 2); /* Packed float-to-word helper JSR. */
    if (n->symbol == -BI_PRINT)
      for (arg = n->b; arg; arg = c->nodes[arg].next)
        if (c->nodes[arg].type == TY_FLOAT)
          result = maximum(result, 32);
    if (n->symbol == -BI_LEN &&
        (c->nodes[n->b].type == TY_STR || c->nodes[n->b].type == TY_BYTES))
      result = maximum(result, 2); /* Runtime string length JSR. */
    if (n->symbol <= -BI_FILE_OPEN && n->symbol >= -BI_FILE_TEXT)
      result = maximum(result, 2); /* Inline I/O calls into KERNAL. */
    if (n->symbol >= 0)
      result = maximum(result, 2 + function_cost(c, n->symbol, cache));
    break;
  case N_LIST:
    for (arg = n->a; arg; arg = c->nodes[arg].next)
      result = maximum(result, expression_cost(c, arg, cache));
    break;
  default:
    break;
  }
  if (result > 128)
    fail(c, n->token,
         "expression needs %d hardware stack bytes (limit 128); split it into "
         "smaller expressions",
         result);
  return result;
}
static int block_cost(Compiler *c, int i, int *cache) {
  int result = 0, cost;
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    cost = 0;
    switch (n->kind) {
    case N_ASSIGN:
      cost = expression_cost(c, n->b, cache);
      if (n->type == TY_FLOAT)
        cost = maximum(cost, 5);
      if (c->nodes[n->a].kind == N_INDEX)
        cost = maximum(cost, (n->type == TY_FLOAT ? 5 : 2) +
                                 expression_cost(c, c->nodes[n->a].b, cache));
      break;
    case N_EXPR:
    case N_RETURN:
      cost = expression_cost(c, n->a, cache);
      break;
    case N_ASSERT:
      cost = expression_cost(c, n->a, cache);
      if (n->b)
        cost = maximum(cost, maximum(4, expression_cost(c, n->b, cache)));
      break;
    case N_IF:
    case N_WHILE:
      cost =
          maximum(expression_cost(c, n->a, cache), block_cost(c, n->b, cache));
      if (n->kind == N_IF)
        cost = maximum(cost, block_cost(c, n->c, cache));
      else
        cost = maximum(cost, block_cost(c, n->else_body, cache));
      break;
    case N_FOR: {
      int args[3] = {n->b, n->c, n->d}, j, count = n->d ? 3 : n->c ? 2 : 1;
      cost = block_cost(c, (int)n->value, cache);
      for (j = 0; j < count; ++j)
        cost = maximum(cost, j * 2 + expression_cost(c, args[j], cache));
      if (n->op)
        cost = maximum(cost, count * 2);
      cost = maximum(cost, block_cost(c, n->else_body, cache));
      break;
    }
    case N_EACH: {
      cost = maximum(expression_cost(c, n->b, cache),
                     block_cost(c, (int)n->value, cache));
      if (c->symbols[n->symbol].type == TY_FLOAT)
        cost = maximum(cost, 32);
      cost = maximum(cost, block_cost(c, n->else_body, cache));
      break;
    }
    default:
      break;
    }
    result = maximum(result, cost);
    if (result > 128)
      fail(c, n->token, "statement exceeds the 128-byte hardware stack budget");
  }
  return result;
}
void validate_ir(Compiler *c) {
  int cache[MAX_FUNCTIONS], i;
  validate_irq(c);
  for (i = 0; i < MAX_FUNCTIONS; ++i)
    cache[i] = -1;
  for (i = 0; i < c->nfunctions; ++i) {
    int cost = function_cost(c, i, cache);
    if (c->functions[i].irq && cost > 80)
      fail(c, c->nodes[c->functions[i].node].token,
           "IRQ callback needs %d stack bytes (limit 80 alongside foreground "
           "execution)",
           cost);
  }
  block_cost(c, c->root, cache);
}

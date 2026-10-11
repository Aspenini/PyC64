#include "pyc64.h"

/* Keep the temporary binary buffer outside the recursive folding frame. */
static void fold_bytes_value(Compiler *c, int i) {
  unsigned char data[MAX_BYTES];
  Node *n = &c->nodes[i], *left = &c->nodes[n->a], *right = &c->nodes[n->b];
  int count, j;
  if (n->op == '+') {
    count = left->d + right->d;
    if (count > MAX_BYTES - 2) {
      fail(c, n->token, "bytes concatenation exceeds the literal arena");
      return;
    }
    memcpy(data, c->bytes + left->value, (size_t)left->d);
    memcpy(data + left->d, c->bytes + right->value, (size_t)right->d);
  } else {
    Node *source = left->kind == N_BYTES ? left : right;
    long repetitions = left->kind == N_BYTES ? right->value : left->value;
    if (repetitions <= 0 || !source->d)
      count = 0;
    else {
      if (repetitions > (MAX_BYTES - 2) / source->d) {
        fail(c, n->token, "bytes repetition exceeds the literal arena");
        return;
      }
      count = source->d * (int)repetitions;
      for (j = 0; j < count; j += source->d)
        memcpy(data + j, c->bytes + source->value, (size_t)source->d);
    }
  }
  bytes_literal(c, i, data, count);
}

/* Fold literal trees, including resolved named constants, retaining
   mathematical values so assigning 255 + 1 to u8 reports overflow rather than
   silently wrapping. */
static void fold(Compiler *c, int i, int depth, int strict) {
  Node *n;
  int64_t a, b, v = 0;
  int ok = 1;
  if (!i)
    return;
  if (depth > MAX_DEPTH)
    fail(c, c->nodes[i].token, "expression nesting is too deep");
  n = &c->nodes[i];
  if (n->kind == N_CALL && c->nodes[n->a].kind == N_NAME && n->b) {
    char name[192];
    int module;
    int left = n->b, right = c->nodes[left].next;
    resolve_name(c, n->a, &module, name, sizeof(name));
    if (!strcmp(name, "pow") && right && !c->nodes[right].next) {
      n->kind = N_BINARY;
      n->op = T_POWER;
      n->c = 1; /* Retain call origin for expression-statement validation. */
      n->a = left;
      n->b = right;
      c->nodes[left].next = 0;
    }
  }
  /* Floating expressions are rounded by the target ROM. Keep them native
     instead of folding them with the host's different precision. */
  if (n->kind != N_BINARY && n->kind != N_UNARY)
    return;
  fold(c, n->a, depth + 1, strict);
  if (c->nodes[n->a].kind == N_STRING || c->nodes[n->a].kind == N_BYTES) {
    Node *left = &c->nodes[n->a];
    if (n->kind == N_UNARY && n->op == OP_NOT) {
      n->kind = N_CONST;
      n->value = !left->d;
      n->type = TY_BOOL;
      n->a = n->b = 0;
      return;
    }
    if (n->kind == N_BINARY && (n->op == OP_AND || n->op == OP_OR)) {
      int next = n->next, token = n->token;
      int selected = (n->op == OP_AND ? !left->d : left->d) ? n->a : n->b;
      fold(c, selected, depth + 1, strict);
      *n = c->nodes[selected];
      n->next = next;
      n->token = token;
      return;
    }
  }
  if (n->kind == N_BINARY && c->nodes[n->a].kind == N_CONST &&
      ((n->op == OP_AND && !c->nodes[n->a].value) ||
       (n->op == OP_OR && c->nodes[n->a].value))) {
    n->kind = N_CONST;
    n->value = c->nodes[n->a].value;
    n->type = c->nodes[n->a].type;
    n->a = n->b = 0;
    return;
  }
  fold(c, n->b, depth + 1, strict);
  if (n->kind == N_BINARY && (n->op == OP_IN || n->op == OP_NOTIN) &&
      c->nodes[n->b].kind == N_BYTES &&
      (c->nodes[n->a].kind == N_BYTES || c->nodes[n->a].kind == N_CONST)) {
    Node *needle = &c->nodes[n->a], *source = &c->nodes[n->b];
    int found = 0, j;
    if (needle->kind == N_CONST) {
      if (needle->value < 0 || needle->value > 255) {
        fail(c, n->token, "integer byte needles must be in 0..255");
        return;
      }
      found = memchr(c->bytes + source->value, (int)needle->value,
                     (size_t)source->d) != NULL;
    } else {
      for (j = 0; j <= source->d - needle->d; ++j)
        if (!memcmp(c->bytes + source->value + j, c->bytes + needle->value,
                    (size_t)needle->d)) {
          found = 1;
          break;
        }
    }
    n->kind = N_CONST;
    n->type = TY_BOOL;
    n->value = n->op == OP_IN ? found : !found;
    n->a = n->b = 0;
    return;
  }
  if (n->kind == N_BINARY &&
      ((n->op == '+' && c->nodes[n->a].kind == N_BYTES &&
        c->nodes[n->b].kind == N_BYTES) ||
       (n->op == '*' &&
        ((c->nodes[n->a].kind == N_BYTES && c->nodes[n->b].kind == N_CONST) ||
         (c->nodes[n->b].kind == N_BYTES &&
          c->nodes[n->a].kind == N_CONST))))) {
    fold_bytes_value(c, i);
    return;
  }
  if (n->kind == N_BINARY && c->nodes[n->a].kind == N_BYTES &&
      c->nodes[n->b].kind == N_BYTES && n->op != OP_AND && n->op != OP_OR) {
    Node *left = &c->nodes[n->a], *right = &c->nodes[n->b];
    int length = left->d < right->d ? left->d : right->d;
    int order =
        memcmp(c->bytes + left->value, c->bytes + right->value, (size_t)length);
    if (!order)
      order = left->d < right->d ? -1 : left->d > right->d;
    switch (n->op) {
    case T_EQ:
      v = order == 0;
      break;
    case T_NE:
      v = order != 0;
      break;
    case '<':
      v = order < 0;
      break;
    case '>':
      v = order > 0;
      break;
    case T_LE:
      v = order <= 0;
      break;
    case T_GE:
      v = order >= 0;
      break;
    default:
      return;
    }
    n->kind = N_CONST;
    n->type = TY_BOOL;
    n->value = (long)v;
    n->a = n->b = 0;
    return;
  }
  if (n->kind == N_BINARY && c->nodes[n->a].kind == N_STRING &&
      c->nodes[n->b].kind == N_STRING) {
    int order = strcmp(c->strings + c->nodes[n->a].value,
                       c->strings + c->nodes[n->b].value);
    switch (n->op) {
    case T_EQ:
      v = order == 0;
      break;
    case T_NE:
      v = order != 0;
      break;
    case '<':
      v = order < 0;
      break;
    case '>':
      v = order > 0;
      break;
    case T_LE:
      v = order <= 0;
      break;
    case T_GE:
      v = order >= 0;
      break;
    default:
      return;
    }
    n->kind = N_CONST;
    n->value = (long)v;
    n->type = TY_BOOL;
    n->a = n->b = 0;
    return;
  }
  if (c->nodes[n->a].kind != N_CONST ||
      (n->kind == N_BINARY && c->nodes[n->b].kind != N_CONST))
    return;
  a = c->nodes[n->a].value;
  b = n->b ? c->nodes[n->b].value : 0;
  if (a < -32768 || a > 65535 || b < -32768 || b > 65535)
    return;
  if (n->kind == N_UNARY) {
    switch (n->op) {
    case '-':
      v = -a;
      break;
    case '+':
      v = a;
      break;
    case '~':
      v = ~a;
      break;
    case OP_NOT:
      v = !a;
      break;
    default:
      ok = 0;
    }
  } else
    switch (n->op) {
    case '+':
      v = a + b;
      break;
    case '-':
      v = a - b;
      break;
    case '*':
      v = a * b;
      break;
    case T_POWER:
      if (b < 0)
        return;
      v = 1;
      while (b) {
        if (b & 1)
          v *= a;
        b >>= 1;
        if (v < -32768 || v > 65535)
          break;
        if (b) {
          a *= a;
          if (a > 65535) {
            v = 65536;
            break;
          }
        }
      }
      break;
    case T_FLOORDIV:
    case '%':
      if (!b) {
        if (strict)
          fail(c, n->token, "division by zero");
        return;
      }
      v = a / b;
      if (a % b && ((a < 0) != (b < 0)))
        --v;
      if (n->op == '%')
        v = a - v * b;
      break;
    case '&':
      v = a & b;
      break;
    case '|':
      v = a | b;
      break;
    case '^':
      v = a ^ b;
      break;
    case T_SHL:
    case T_SHR:
      if (b < 0 || b > 15) {
        if (strict)
          fail(c, n->token, "shift count must be between 0 and 15");
        return;
      }
      if (n->op == T_SHL)
        v = a * (INT64_C(1) << b);
      else {
        v = a / (INT64_C(1) << b);
        if (a < 0 && a % (INT64_C(1) << b))
          --v;
      }
      break;
    case T_EQ:
      v = a == b;
      break;
    case T_NE:
      v = a != b;
      break;
    case '<':
      v = a < b;
      break;
    case '>':
      v = a > b;
      break;
    case T_LE:
      v = a <= b;
      break;
    case T_GE:
      v = a >= b;
      break;
    case OP_AND:
      v = a ? b : a;
      break;
    case OP_OR:
      v = a ? a : b;
      break;
    default:
      ok = 0;
    }
  if (ok) {
    if (v < -32768 || v > 65535) {
      if (strict)
        fail(c, n->token,
             "constant expression exceeds the 16-bit integer range");
      return;
    }
    n->kind = N_CONST;
    n->value = (long)v;
    if (n->op == OP_AND || n->op == OP_OR)
      n->type =
          c->nodes[(n->op == OP_AND ? a != 0 : a == 0) ? n->b : n->a].type;
    if (n->op == OP_NOT || n->op == T_EQ || n->op == T_NE || n->op == '<' ||
        n->op == '>' || n->op == T_LE || n->op == T_GE)
      n->type = TY_BOOL;
  }
}
void fold_expression(Compiler *c, int node) { fold(c, node, 0, 1); }
void optimize(Compiler *c) {
  int i;
  for (i = 1; i <= c->nnodes; ++i)
    fold(c, i, 0, 0);
}

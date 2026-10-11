#include "pyc64.h"

/* Solve scalar signatures before semantic analysis mutates the tree. Types only
   widen; all call sites participate, including forward and imported calls. */
typedef struct {
  char name[64];
  int node, scope, module, type, element, annotated;
} Binding;
typedef struct {
  char name[192];
  int node, module, params[16], argc, type, annotated, state, called;
  int value_return;
} Signature;
typedef struct {
  Compiler *c;
  Binding bindings[MAX_SYMBOLS];
  Signature functions[MAX_FUNCTIONS];
  int nbindings, nfunctions, changed;
} Inference;
static int integer(int t) {
  return (t >= TY_U8 && t <= TY_I16) || enum_type(t);
}
static int join(Inference *g, int a, int b, int token) {
  if (!a || a == b)
    return b ? b : a;
  if (!b)
    return a;
  if (enum_type(a) || enum_type(b))
    fail(g->c, token, "inferred values require the same IntEnum type");
  if ((a == TY_FLOAT && (integer(b) || b == TY_BOOL)) ||
      (b == TY_FLOAT && (integer(a) || a == TY_BOOL)))
    return TY_FLOAT;
  if (!integer(a) || !integer(b) || (a == TY_U16 && type_signed(b)) ||
      (b == TY_U16 && type_signed(a)))
    fail(g->c, token,
         "incompatible inferred types %s and %s; add explicit annotations or "
         "casts",
         type_name(a), type_name(b));
  if (type_signed(a) != type_signed(b))
    return TY_I16;
  return type_size(a) >= type_size(b) ? a : b;
}
static void update(Inference *g, int *slot, int type, int token) {
  int result = join(g, *slot, type, token);
  if (result != *slot) {
    *slot = result;
    g->changed = 1;
  }
}
static int binding(Inference *g, const char *name, int scope, int module) {
  int j;
  for (j = 0; j < g->nbindings; ++j)
    if (g->bindings[j].scope == scope && g->bindings[j].module == module &&
        !strcmp(g->bindings[j].name, name))
      return j;
  return -1;
}
static int declare(Inference *g, int node, int name_node, int scope, int type,
                   int element) {
  Compiler *c = g->c;
  char name[64];
  int module = c->tokens[c->nodes[name_node].token].module, j;
  token_text(c, c->nodes[name_node].token, name, sizeof(name));
  j = binding(g, name, scope, module);
  if (j >= 0)
    return j;
  if (g->nbindings == MAX_SYMBOLS)
    fail(c, c->nodes[node].token, "symbol arena exhausted");
  j = g->nbindings++;
  strcpy(g->bindings[j].name, name);
  g->bindings[j].node = node;
  g->bindings[j].scope = scope;
  g->bindings[j].module = module;
  g->bindings[j].type = type;
  g->bindings[j].element = element;
  g->bindings[j].annotated = type != 0;
  return j;
}
static void collect(Inference *g, int i, int scope) {
  for (; i; i = g->c->nodes[i].next) {
    Node *n = &g->c->nodes[i];
    if ((n->kind == N_ASSIGN || n->kind == N_DECLCONST) &&
        g->c->nodes[n->a].kind == N_NAME) {
      int type = n->type;
      if (!type && n->b && g->c->nodes[n->b].kind == N_CALL) {
        int candidate = library_type(g->c, g->c->nodes[n->b].a);
        if (object_type(candidate))
          type = candidate;
      }
      declare(g, i, n->a, scope, type, n->c);
    }
    if (n->kind == N_FOR || n->kind == N_EACH) {
      declare(g, i, n->a, scope, n->kind == N_FOR ? TY_I16 : TY_NONE, 0);
      collect(g, (int)n->value, scope);
      collect(g, n->else_body, scope);
    } else if (n->kind == N_IF || n->kind == N_WHILE) {
      collect(g, n->b, scope);
      if (n->kind == N_IF)
        collect(g, n->c, scope);
      else
        collect(g, n->else_body, scope);
    } else if (n->kind == N_RETURN && n->a && scope)
      g->functions[scope - 1].value_return = 1;
  }
}
static int function(Inference *g, int module, const char *name) {
  int j;
  for (j = 0; j < g->nfunctions; ++j)
    if (g->functions[j].module == module && !strcmp(g->functions[j].name, name))
      return j;
  return -1;
}
static int lookup(Inference *g, int node, int scope) {
  char name[192];
  int module, j, owner = g->c->tokens[g->c->nodes[node].token].module;
  resolve_name(g->c, node, &module, name, sizeof(name));
  j = module == owner ? binding(g, name, scope, module) : -1;
  return j >= 0 ? j : binding(g, name, 0, module);
}
static int expression(Inference *g, int node, int scope, int depth);
static int byte_receiver(Inference *g, Node *n, int scope) {
  Compiler *c = g->c;
  return c->nodes[n->a].kind == N_ATTR &&
         !module_namespace(c, c->nodes[n->a].a) &&
         expression(g, c->nodes[n->a].a, scope, 0) == TY_BYTES;
}
static int receiver_type(Inference *g, int node, int scope) {
  Compiler *c = g->c;
  Node *n = &c->nodes[node];
  char name[192], part[64];
  int owner = c->tokens[n->token].module, j, root = node, stack[MAX_DEPTH],
      count = 0;
  if (n->kind == N_NAME) {
    token_text(c, n->token, name, sizeof(name));
    j = binding(g, name, scope, owner);
    if (j < 0)
      j = binding(g, name, 0, owner);
    if (j < 0 && !module_namespace(c, node))
      j = lookup(g, node, scope);
    return j >= 0 ? g->bindings[j].type : TY_NONE;
  }
  if (n->kind != N_ATTR)
    return TY_NONE;
  while (c->nodes[root].kind == N_ATTR) {
    if (count == MAX_DEPTH)
      fail(c, n->token, "qualified name is too deeply nested");
    stack[count++] = root;
    root = c->nodes[root].a;
  }
  if (c->nodes[root].kind != N_NAME)
    return TY_NONE;
  token_text(c, c->nodes[root].token, name, sizeof(name));
  while (count) {
    token_text(c, c->nodes[stack[--count]].token, part, sizeof(part));
    if (strlen(name) + strlen(part) + 2 >= sizeof(name))
      fail(c, n->token, "qualified name too long");
    strcat(name, ".");
    strcat(name, part);
  }
  for (j = 0; j < c->nimports; ++j) {
    Import *im = &c->imports[j];
    char module_name[192];
    snprintf(module_name, sizeof(module_name), "%s%s%s", im->alias,
             *im->prefix ? "." : "", im->prefix);
    if (im->module == owner && !*im->member && !strcmp(name, module_name))
      return TY_NONE;
  }
  j = lookup(g, node, scope);
  return j >= 0 ? g->bindings[j].type : TY_NONE;
}
static int call_target(Inference *g, Node *n, int scope, int *constructor,
                       int *receiver) {
  Compiler *c = g->c;
  char name[192], member[64];
  int module, type;
  *constructor = *receiver = 0;
  if (c->nodes[n->a].kind == N_ATTR) {
    int base = c->nodes[n->a].a;
    type = receiver_type(g, base, scope);
    if (object_type(type)) {
      ClassLayout *layout = &c->classes[type - TY_CLASS_BASE];
      token_text(c, c->nodes[n->a].token, member, sizeof(member));
      snprintf(name, sizeof(name), "%s.%s", layout->name, member);
      *receiver = base;
      return function(g, layout->module, name);
    }
  }
  resolve_name(c, n->a, &module, name, sizeof(name));
  type = class_named(c, module, name);
  if (type) {
    *constructor = type;
    snprintf(name, sizeof(name), "%s.__init__",
             c->classes[type - TY_CLASS_BASE].name);
  }
  return function(g, module, name);
}
static int property_target(Inference *g, int node, int scope, int writing) {
  Node *n = &g->c->nodes[node];
  int type, accessor, j;
  if (n->kind != N_ATTR)
    return -1;
  type = receiver_type(g, n->a, scope);
  if (!object_type(type))
    return -1;
  accessor = class_property(g->c, type, n->token, writing);
  if (accessor <= 0)
    return -1;
  for (j = 0; j < g->nfunctions; ++j)
    if (g->functions[j].node == accessor)
      return j;
  return -1;
}
static void activate(Inference *g, int index) {
  if (!g->functions[index].called) {
    g->functions[index].called = 1;
    g->c->nodes[g->functions[index].node].op = 0;
    g->changed = 1;
  }
}
static int argument_element(Inference *g, int node, int scope) {
  Node *n = &g->c->nodes[node];
  if (n->kind == N_NAME || n->kind == N_ATTR) {
    int j = lookup(g, node, scope);
    return j >= 0 ? g->bindings[j].element : TY_NONE;
  }
  return TY_NONE;
}
static int list_element(Inference *g, int node, int scope, int depth) {
  int j, type = 0;
  for (j = g->c->nodes[node].a; j; j = g->c->nodes[j].next) {
    int item = expression(g, j, scope, depth + 1);
    if (item == TY_ARRAY || item == TY_LIST || item == TY_VOID ||
        object_type(item))
      fail(g->c, g->c->nodes[j].token,
           "list elements must be integers, bool or str");
    type = join(g, type, item, g->c->nodes[j].token);
  }
  return type;
}
static int iteration_element(Inference *g, int node, int scope) {
  Node *n = &g->c->nodes[node];
  int type = expression(g, node, scope, 0), j;
  if (type == TY_STR)
    return TY_STR;
  if (type == TY_BYTES)
    return TY_U8;
  if (n->kind == N_LIST)
    return n->d ? list_element(g, node, scope, 0) : TY_U8;
  if (n->kind == N_CALL && type == TY_ARRAY)
    return library_type(g->c, n->b);
  if (n->kind == N_NAME || n->kind == N_ATTR) {
    j = lookup(g, node, scope);
    if (j >= 0) {
      Binding *v = &g->bindings[j];
      Node *definition = &g->c->nodes[v->node];
      if (v->element)
        return v->element;
      if (type == TY_LIST && definition->kind == N_ASSIGN && definition->b &&
          g->c->nodes[definition->b].kind == N_LIST &&
          !g->c->nodes[definition->b].d)
        return TY_U8; /* No element will be loaded from an untyped empty list.
                       */
    }
  }
  return TY_NONE;
}
static int expression(Inference *g, int node, int scope, int depth) {
  Compiler *c = g->c;
  Node *n;
  int a, b, j, module;
  char name[192];
  if (!node)
    return TY_VOID;
  n = &c->nodes[node];
  if (depth > MAX_DEPTH)
    fail(c, n->token, "expression nesting is too deep");
  switch (n->kind) {
  case N_NONE:
    return TY_VOID;
  case N_CONST:
    return n->type        ? n->type
           : n->value < 0 ? (n->value >= -128 ? TY_I8 : TY_I16)
                          : (n->value <= 255 ? TY_U8 : TY_U16);
  case N_STRING:
    return TY_STR;
  case N_BYTES:
    return TY_BYTES;
  case N_FLOAT:
  case N_FLOAT_CAST:
    return TY_FLOAT;
  case N_ENUM_LOOKUP:
    expression(g, n->a, scope, depth + 1);
    return n->op == 1 ? TY_STR : n->c;
  case N_ENUM_VALUE:
    return integer_type(expression(g, n->a, scope, depth + 1));
  case N_LIST:
    list_element(g, node, scope, depth);
    return TY_LIST;
  case N_NAME:
  case N_ATTR:
    if (n->kind == N_ATTR) {
      a = receiver_type(g, n->a, scope);
      if (!object_type(a) && !a)
        a = expression(g, n->a, scope, depth + 1);
      if (enum_type(a)) {
        char member[64];
        token_text(c, n->token, member, sizeof(member));
        return !strcmp(member, "value")  ? integer_type(a)
               : !strcmp(member, "name") ? TY_STR
                                         : TY_NONE;
      }
      if (c->nodes[n->a].kind != N_NAME && c->nodes[n->a].kind != N_ATTR)
        return TY_NONE; /* A called function's return may still be unresolved.
                         */
      if (object_type(a)) {
        j = property_target(g, node, scope, 0);
        if (j >= 0) {
          activate(g, j);
          return g->functions[j].type;
        }
        b = class_field(c, a, n->token);
        return c->nodes[c->classes[a - TY_CLASS_BASE].fields[b]].type;
      }
    }
    j = lookup(g, node, scope);
    if (j >= 0)
      return g->bindings[j].type;
    resolve_name(c, node, &module, name, sizeof(name));
    if (!strncmp(name, "c64.input.joystick.", 19))
      return TY_BOOL;
    if (!strcmp(name, "c64.video.screen.border") ||
        !strcmp(name, "c64.video.screen.background"))
      return TY_U8;
    return TY_NONE; /* Final semantic analysis diagnoses unknown names. */
  case N_SLICE:
    a = expression(g, n->a, scope, depth + 1);
    expression(g, n->b, scope, depth + 1);
    expression(g, n->c, scope, depth + 1);
    expression(g, n->d, scope, depth + 1);
    return a ? TY_BYTES : TY_NONE;
  case N_INDEX:
    a = expression(g, n->a, scope, depth + 1);
    expression(g, n->b, scope, depth + 1);
    if (a == TY_STR)
      return TY_STR;
    if (a == TY_BYTES)
      return TY_U8;
    if (c->nodes[n->a].kind == N_LIST)
      return list_element(g, n->a, scope, depth + 1);
    if (c->nodes[n->a].kind == N_NAME || c->nodes[n->a].kind == N_ATTR) {
      j = lookup(g, n->a, scope);
      return j >= 0 ? g->bindings[j].element : TY_NONE;
    }
    return TY_NONE;
  case N_UNARY:
    a = expression(g, n->a, scope, depth + 1);
    return n->op == OP_NOT ? TY_BOOL
           : a == TY_BOOL  ? (n->op == '-' ? TY_I8 : TY_U8)
           : enum_type(a) && (n->op == '-' || n->op == '~') ? TY_I16
                                                            : integer_type(a);
  case N_BINARY:
    a = expression(g, n->a, scope, depth + 1);
    b = expression(g, n->b, scope, depth + 1);
    if (n->op == T_EQ || n->op == T_NE || n->op == '<' || n->op == '>' ||
        n->op == T_LE || n->op == T_GE || n->op == OP_IN || n->op == OP_NOTIN)
      return TY_BOOL;
    if (!a || !b)
      return TY_NONE;
    if ((a == TY_BYTES || b == TY_BYTES) && (n->op == '+' || n->op == '*'))
      return TY_BYTES; /* Final semantic analysis validates the other operand.
                        */
    if ((n->op == OP_AND || n->op == OP_OR) && a == b && enum_type(a))
      return a;
    if ((n->op == OP_AND || n->op == OP_OR) && (enum_type(a) || enum_type(b)))
      fail(c, n->token, "IntEnum boolean operands require matching types");
    a = integer_type(a);
    b = integer_type(b);
    if (n->op == '/')
      return TY_FLOAT;
    if (n->op == T_POWER)
      return power_type(c, a, b, n->b);
    if (n->op == T_SHL || n->op == T_SHR)
      return a == TY_BOOL ? TY_U8 : a;
    if ((n->op == OP_AND || n->op == OP_OR) && a == TY_BOOL && b == TY_BOOL)
      return TY_BOOL;
    return join(g, a == TY_BOOL ? TY_U8 : a, b == TY_BOOL ? TY_U8 : b,
                n->token);
  case N_CALL: {
    int arg, count = 0, types[16], constructor, receiver, offset;
    int callback = irq_registration(c, node);
    if (callback) {
      for (j = 0; j < g->nfunctions; ++j)
        if (g->functions[j].node == callback)
          activate(g, j);
      expression(g, n->b, scope, depth + 1);
      return TY_BOOL;
    }
    if (c->nodes[n->a].kind == N_ATTR &&
        !module_namespace(c, c->nodes[n->a].a)) {
      int base = c->nodes[n->a].a, base_type, class_base = 0;
      if (named_expression(c, base)) {
        resolve_name(c, base, &module, name, sizeof(name));
        class_base = class_named(c, module, name);
      }
      if (!class_base) {
        base_type = expression(g, base, scope, depth + 1);
        if (!base_type)
          return TY_NONE;
        if (base_type == TY_BYTES) {
          int method;
          token_text(c, c->nodes[n->a].token, name, sizeof(name));
          method = bytes_method(name);
          if (!method)
            fail(c, n->token, "unsupported bytes method '%s'", name);
          for (arg = n->b; arg; arg = c->nodes[arg].next)
            expression(g, arg, scope, depth + 1);
          return bytes_method_type(method);
        }
      }
    }
    resolve_name(c, n->a, &module, name, sizeof(name));
    for (arg = n->b; arg; arg = c->nodes[arg].next) {
      if (count == 16)
        fail(c, n->token, "at most 16 arguments are supported");
      /* array's first argument names a type, rather than a value. */
      if (!strcmp(name, "bytes") && arg == n->b &&
          c->nodes[arg].kind == N_LIST) {
        int item;
        for (item = c->nodes[arg].a; item; item = c->nodes[item].next)
          expression(g, item, scope, depth + 1);
        types[count++] = TY_LIST;
        continue;
      }
      types[count++] = !strcmp(name, "pyc64.types.array") && arg == n->b
                           ? TY_NONE
                           : expression(g, arg, scope, depth + 1);
    }
    j = call_target(g, n, scope, &constructor, &receiver);
    offset = constructor || receiver ? 1 : 0;
    if (j >= 0) {
      Signature *f = &g->functions[j];
      activate(g, j);
      n->symbol = j;
      if (count + offset != f->argc)
        fail(c, n->token, "function '%s' expects %d arguments", name,
             f->argc - offset);
      for (a = 0; a < count; ++a) {
        Binding *p = &g->bindings[f->params[a + offset]];
        if (!p->annotated) {
          if (types[a] == TY_VOID)
            fail(c, n->token,
                 "cannot pass a function with no return value as an argument");
          update(g, &p->type, types[a], n->token);
        }
        if (types[a] == TY_LIST || types[a] == TY_ARRAY) {
          int actual = n->b, index, element;
          for (index = 0; index < a; ++index)
            actual = c->nodes[actual].next;
          element = argument_element(g, actual, scope);
          if (!p->annotated && element)
            update(g, &p->element, element, n->token);
        }
      }
      return constructor ? constructor : f->type;
    }
    if (constructor) {
      if (count)
        fail(c, n->token, "class without __init__ expects no arguments");
      return constructor;
    }
    if (c->nodes[n->a].kind == N_ATTR &&
        c->nodes[c->nodes[n->a].a].kind == N_NAME &&
        !module_namespace(c, c->nodes[n->a].a) &&
        lookup(g, c->nodes[n->a].a, scope) >= 0 &&
        !receiver_type(g, c->nodes[n->a].a, scope))
      return TY_NONE;
    if (!strcmp(name, "int")) {
      if (count > 1)
        fail(c, n->token,
             "int expects zero or one argument; bases are unsupported");
      return !count || types[0] == TY_FLOAT || types[0] == TY_STR ? TY_I16
             : types[0] == TY_BOOL ? TY_U8
                                   : integer_type(types[0]);
    }
    a = library_type(c, n->a);
    if (a)
      return a;
    if (!strcmp(name, "len"))
      return TY_U16;
    if (asset_kind(name))
      return TY_BYTES;
    if (!strcmp(name, "c64.assets.address"))
      return TY_U16;
    if (!strcmp(name, "c64._music.ticks"))
      return TY_U16;
    if (!strcmp(name, "c64._music.start") || !strcmp(name, "c64._music.stop") ||
        !strcmp(name, "c64._music.active"))
      return TY_BOOL;
    if (!strcmp(name, "ord"))
      return TY_U8;
    if (!strcmp(name, "chr") || !strcmp(name, "input"))
      return TY_STR;
    if (!strcmp(name, "pow")) {
      if (count != 2)
        fail(c, n->token, "pow expects two arguments");
      return power_type(c, types[0], types[1], c->nodes[n->b].next);
    }
    if (!strncmp(name, "pyc64._math.", 12))
      return !strcmp(name + 12, "floor") || !strcmp(name + 12, "ceil") ||
                     !strcmp(name + 12, "trunc")
                 ? TY_I16
                 : TY_FLOAT;
    if (!strcmp(name, "c64._reu.transfer") ||
        !strcmp(name, "c64.memory.peek") ||
        !strcmp(name, "c64.keyboard.read") ||
        !strcmp(name, "c64.kernal.read_byte") ||
        !strcmp(name, "c64.kernal.status") || !strcmp(name, "c64.kernal.error"))
      return TY_U8;
    if (!strcmp(name, "c64.irq.active") || !strcmp(name, "c64.irq.disable") ||
        !strcmp(name, "c64.irq.off") || !strcmp(name, "c64.irq.set_line") ||
        !strcmp(name, "c64._reu.present") || !strcmp(name, "c64.kernal.open") ||
        !strcmp(name, "c64.kernal.write_byte") ||
        !strcmp(name, "c64.kernal.write_text"))
      return TY_BOOL;
    if (!strcmp(name, "c64._video.mode") || !strcmp(name, "c64.irq.restore") ||
        !strcmp(name, "print") || !strcmp(name, "c64.memory.poke") ||
        !strcmp(name, "c64.keyboard.clear") ||
        !strcmp(name, "c64.kernal.close") ||
        !strcmp(name, "c64.video.screen.clear") ||
        !strcmp(name, "c64.video.screen.put_char") ||
        !strcmp(name, "c64.sprite.enable") ||
        !strcmp(name, "c64.sprite.disable") ||
        !strcmp(name, "c64.sprite.position") ||
        !strcmp(name, "c64.sprite.color") || !strcmp(name, "c64.sprite.data"))
      return TY_VOID;
    fail(c, n->token, "unsupported or unknown function '%s'", name);
    return TY_NONE;
  }
  default:
    return TY_NONE;
  }
}
static void solve_block(Inference *g, int i, int scope) {
  for (; i; i = g->c->nodes[i].next) {
    Node *n = &g->c->nodes[i];
    int type, j;
    switch (n->kind) {
    case N_ASSIGN:
    case N_DECLCONST:
      type = expression(g, n->b, scope, 0);
      if (g->c->nodes[n->a].kind != N_NAME) {
        j = property_target(g, n->a, scope, 1);
        if (j >= 0) {
          Binding *value = &g->bindings[g->functions[j].params[1]];
          activate(g, j);
          if (!value->annotated)
            update(g, &value->type, type, n->token);
          break;
        }
        expression(g, n->a, scope, 0);
        break;
      }
      j = lookup(g, n->a, scope);
      if (j >= 0 && g->bindings[j].node == i) {
        Binding *v = &g->bindings[j];
        if (!v->annotated)
          update(g, &v->type, type, n->token);
        if (type == TY_LIST && !n->c && g->c->nodes[n->b].kind == N_LIST)
          update(g, &v->element, list_element(g, n->b, scope, 0), n->token);
        if (type == TY_ARRAY && !n->c && g->c->nodes[n->b].kind == N_CALL)
          v->element = library_type(g->c, g->c->nodes[n->b].b);
      }
      break;
    case N_EXPR:
      expression(g, n->a, scope, 0);
      break;
    case N_ASSERT:
      expression(g, n->a, scope, 0);
      if (n->b)
        expression(g, n->b, scope, 0);
      break;
    case N_RETURN:
      type = expression(g, n->a, scope, 0);
      if (scope && !g->functions[scope - 1].annotated) {
        if (object_type(type))
          fail(g->c, n->token, "object return values are unsupported");
        if (type == TY_ARRAY || type == TY_LIST)
          fail(g->c, n->token, "list/array return values are unsupported");
        update(g, &g->functions[scope - 1].type, type, n->token);
      }
      break;
    case N_IF:
    case N_WHILE:
      expression(g, n->a, scope, 0);
      solve_block(g, n->b, scope);
      if (n->kind == N_IF)
        solve_block(g, n->c, scope);
      else
        solve_block(g, n->else_body, scope);
      break;
    case N_FOR:
      expression(g, n->b, scope, 0);
      if (n->c)
        expression(g, n->c, scope, 0);
      if (n->d)
        expression(g, n->d, scope, 0);
      solve_block(g, (int)n->value, scope);
      solve_block(g, n->else_body, scope);
      break;
    case N_EACH:
      type = iteration_element(g, n->b, scope);
      j = lookup(g, n->a, scope);
      if (j >= 0 && g->bindings[j].node == i)
        update(g, &g->bindings[j].type, type, n->token);
      solve_block(g, (int)n->value, scope);
      solve_block(g, n->else_body, scope);
      break;
    default:
      break;
    }
  }
}
static void check_calls(Inference *g, int i);
static void check_function(Inference *g, int index) {
  Signature *f = &g->functions[index];
  int scope = g->c->scope;
  if (f->state == 1)
    fail(g->c, g->c->nodes[f->node].token,
         "recursion is unsupported with static function frames");
  if (f->state == 2)
    return;
  f->state = 1;
  g->c->scope = index + 1;
  check_calls(g, g->c->nodes[f->node].b);
  g->c->scope = scope;
  f->state = 2;
}
static void check_calls(Inference *g, int i) {
  for (; i; i = g->c->nodes[i].next) {
    Node *n = &g->c->nodes[i];
    if (n->kind == N_ATTR || n->kind == N_ASSIGN) {
      int index = property_target(g, n->kind == N_ASSIGN ? n->a : i,
                                  g->c->scope, n->kind == N_ASSIGN);
      if (index >= 0)
        check_function(g, index);
      if (n->kind == N_ASSIGN) {
        check_calls(g, n->b);
        if (index < 0)
          check_calls(g, n->a);
      }
    } else if (n->kind == N_CALL) {
      if (irq_registration(g->c, i)) {
        check_calls(g, n->b); /* Function selectors are skipped as names. */
        continue;
      }
      if (byte_receiver(g, n, g->c->scope)) {
        check_calls(g, g->c->nodes[n->a].a);
        check_calls(g, n->b);
        continue;
      }
      int constructor, receiver;
      int index = call_target(g, n, g->c->scope, &constructor, &receiver);
      if (index >= 0)
        check_function(g, index);
      check_calls(g, n->b);
    } else if (n->kind == N_FOR || n->kind == N_EACH) {
      check_calls(g, n->b);
      check_calls(g, n->c);
      check_calls(g, n->d);
      check_calls(g, (int)n->value);
      check_calls(g, n->else_body);
    } else if (n->kind != N_ENUMDEF && n->kind != N_FUNC && n->kind != N_NAME &&
               n->kind != N_ATTR && n->kind != N_PARAM && n->kind != N_CONST &&
               n->kind != N_STRING && n->kind != N_BYTES) {
      check_calls(g, n->a);
      check_calls(g, n->b);
      if (n->kind == N_SLICE) {
        check_calls(g, n->c);
        check_calls(g, n->d);
      }
      if (n->kind == N_IF)
        check_calls(g, n->c);
      if (n->kind == N_WHILE)
        check_calls(g, n->else_body);
    }
  }
}
static void mark_inferred_calls(Inference *g, int i) {
  for (; i; i = g->c->nodes[i].next) {
    Node *n = &g->c->nodes[i];
    if (n->kind == N_ATTR || n->kind == N_ASSIGN) {
      int index = property_target(g, n->kind == N_ASSIGN ? n->a : i,
                                  g->c->scope, n->kind == N_ASSIGN);
      if (index >= 0 && !g->functions[index].called) {
        int scope = g->c->scope;
        activate(g, index);
        g->c->scope = index + 1;
        mark_inferred_calls(g, g->c->nodes[g->functions[index].node].b);
        g->c->scope = scope;
      }
      if (n->kind == N_ASSIGN) {
        mark_inferred_calls(g, n->b);
        if (index < 0)
          mark_inferred_calls(g, n->a);
      }
    } else if (n->kind == N_CALL) {
      int callback = irq_registration(g->c, i), callback_index;
      if (callback) {
        for (callback_index = 0; callback_index < g->nfunctions;
             ++callback_index)
          if (g->functions[callback_index].node == callback &&
              !g->functions[callback_index].called) {
            int scope = g->c->scope;
            activate(g, callback_index);
            g->c->scope = callback_index + 1;
            mark_inferred_calls(g, g->c->nodes[callback].b);
            g->c->scope = scope;
          }
        mark_inferred_calls(g, n->b);
        continue;
      }
      if (g->c->nodes[n->a].kind == N_ATTR &&
          !module_namespace(g->c, g->c->nodes[n->a].a)) {
        mark_inferred_calls(g, g->c->nodes[n->a].a);
        if (!named_expression(g->c, n->a) || byte_receiver(g, n, g->c->scope)) {
          mark_inferred_calls(g, n->b);
          continue;
        }
      }
      int constructor, receiver;
      int index = call_target(g, n, g->c->scope, &constructor, &receiver);
      if (index >= 0 && !g->functions[index].called) {
        int scope = g->c->scope;
        g->functions[index].called = 1;
        g->c->scope = index + 1;
        mark_inferred_calls(g, g->c->nodes[g->functions[index].node].b);
        g->c->scope = scope;
      }
      mark_inferred_calls(g, n->b);
    } else if (n->kind == N_FOR || n->kind == N_EACH) {
      mark_inferred_calls(g, n->b);
      mark_inferred_calls(g, n->c);
      mark_inferred_calls(g, n->d);
      mark_inferred_calls(g, (int)n->value);
      mark_inferred_calls(g, n->else_body);
    } else if (n->kind != N_ENUMDEF && n->kind != N_FUNC && n->kind != N_NAME &&
               n->kind != N_ATTR && n->kind != N_PARAM && n->kind != N_CONST &&
               n->kind != N_STRING && n->kind != N_BYTES) {
      mark_inferred_calls(g, n->a);
      mark_inferred_calls(g, n->b);
      if (n->kind == N_SLICE) {
        mark_inferred_calls(g, n->c);
        mark_inferred_calls(g, n->d);
      }
      if (n->kind == N_IF)
        mark_inferred_calls(g, n->c);
      if (n->kind == N_WHILE)
        mark_inferred_calls(g, n->else_body);
    }
  }
}
void infer_signatures(Compiler *c) {
  Inference *g = (Inference *)calloc(1, sizeof(Inference));
  int i, j, pass;
  if (!g)
    fail(c, 0, "unable to acquire inference arena");
  g->c = c;
  for (i = c->root; i; i = c->nodes[i].next)
    if (c->nodes[i].kind == N_FUNC) {
      Signature *f;
      if (g->nfunctions == MAX_FUNCTIONS)
        fail(c, c->nodes[i].token, "function arena exhausted");
      f = &g->functions[g->nfunctions++];
      f->node = i;
      f->module = c->tokens[c->nodes[i].token].module;
      if (!object_type(c->nodes[i].c))
        check_binding(c, c->nodes[i].token);
      function_name(c, i, f->name, sizeof(f->name));
      for (j = 0; j < g->nfunctions - 1; ++j)
        if (f->module == g->functions[j].module &&
            !strcmp(f->name, g->functions[j].name))
          fail(c, c->nodes[i].token, "duplicate function '%s'", f->name);
      f->type = c->nodes[i].type;
      f->annotated = f->type != 0;
      for (j = c->nodes[i].a; j; j = c->nodes[j].next) {
        check_binding(c, c->nodes[j].token);
        if (f->argc == 16)
          fail(c, c->nodes[j].token, "at most 16 parameters");
        f->params[f->argc++] =
            declare(g, j, j, g->nfunctions, c->nodes[j].type, c->nodes[j].c);
      }
      collect(g, c->nodes[i].b, g->nfunctions);
      if (!f->annotated && !f->value_return)
        f->type = TY_VOID;
    }
  collect(g, c->root, 0);
  for (i = 0; i < g->nfunctions; ++i)
    check_function(g, i);
  mark_inferred_calls(g, c->root);
  /* Keep checking main-module functions, including unused user definitions.
     Imported, unreachable function bodies do not need inferred signatures. */
  for (i = 0; i < g->nfunctions; ++i)
    if (!g->functions[i].module &&
        !object_type(c->nodes[g->functions[i].node].c)) {
      c->scope = i + 1;
      mark_inferred_calls(g, c->nodes[g->functions[i].node].b);
    }
  c->scope = 0;
  for (i = 0; i < g->nfunctions; ++i)
    if ((g->functions[i].module ||
         object_type(c->nodes[g->functions[i].node].c)) &&
        !g->functions[i].called)
      c->nodes[g->functions[i].node].op = 1;
  for (pass = 0; pass < MAX_NODES; ++pass) {
    g->changed = 0;
    solve_block(g, c->root, 0);
    for (i = 0; i < g->nfunctions; ++i)
      if (!c->nodes[g->functions[i].node].op)
        solve_block(g, c->nodes[g->functions[i].node].b, i + 1);
    if (!g->changed)
      break;
  }
  if (pass == MAX_NODES)
    fail(c, 0, "type inference did not converge; add explicit annotations");
  for (i = 0; i < g->nfunctions; ++i)
    g->functions[i].state = 0;
  for (i = 0; i < g->nfunctions; ++i)
    if (!c->nodes[g->functions[i].node].op)
      check_function(g, i);
  for (i = 0; i < g->nfunctions; ++i) {
    Signature *f = &g->functions[i];
    if (c->nodes[f->node].op)
      continue;
    if (!f->type)
      fail(c, c->nodes[f->node].token,
           "cannot infer return type of '%s'; add an explicit annotation",
           f->name);
    if (object_type(f->type))
      fail(c, c->nodes[f->node].token, "object return values are unsupported");
    c->nodes[f->node].type = f->type;
    if (c->nodes[f->node].accessor == 1 && !scalar_type(f->type))
      fail(c, c->nodes[f->node].token,
           "property getter must return a scalar value");
    for (j = 0; j < f->argc; ++j) {
      Binding *p = &g->bindings[f->params[j]];
      if (!p->type)
        fail(c, c->nodes[p->node].token,
             "cannot infer parameter '%s' of '%s'; call it with known types or "
             "add an annotation",
             p->name, f->name);
      c->nodes[p->node].type = p->type;
      c->nodes[p->node].c = p->element;
      if (c->nodes[f->node].accessor == 2 && j == 1 && !scalar_type(p->type))
        fail(c, c->nodes[p->node].token,
             "property setter requires a scalar value");
      if ((p->type == TY_ARRAY || p->type == TY_LIST) && !p->element)
        fail(c, c->nodes[p->node].token,
             "cannot infer container element type of '%s'; annotate the "
             "parameter or its empty argument",
             p->name);
    }
  }
  free(g);
}

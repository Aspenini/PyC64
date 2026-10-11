#include "pyc64.h"

/* Finite IntEnum layouts retain nominal types while using ordinary integer
   storage. The low two bits select u8/i8/u16/i16; no Python objects are needed.
 */
int enum_type(int t) {
  return t >= TY_ENUM_BASE && t < TY_ENUM_BASE + MAX_ENUMS * 4;
}
int integer_type(int t) {
  return enum_type(t) ? TY_U8 + (t - TY_ENUM_BASE) % 4 : t;
}
int enum_named(Compiler *c, int module, const char *name) {
  int j;
  for (j = 0; j < c->nenums; ++j)
    if (c->enums[j].module == module && !strcmp(c->enums[j].name, name))
      return c->enums[j].type;
  return TY_NONE;
}
static int named_tree(Compiler *c, int node) {
  int depth = 0;
  while (c->nodes[node].kind == N_ATTR && ++depth <= MAX_DEPTH)
    node = c->nodes[node].a;
  return c->nodes[node].kind == N_NAME && depth <= MAX_DEPTH;
}
int enum_class(Compiler *c, int node) {
  char name[192];
  int module;
  if (!named_tree(c, node))
    return TY_NONE;
  resolve_name(c, node, &module, name, sizeof(name));
  return enum_named(c, module, name);
}
int enum_value_member(Compiler *c, int type, long value) {
  EnumLayout *e = &c->enums[(type - TY_ENUM_BASE) / 4];
  int j;
  for (j = e->first; j < e->first + e->count; ++j)
    if (c->enum_members[j].value == value)
      return j;
  return -1;
}
static int member_named(Compiler *c, EnumLayout *e, const char *name) {
  int j;
  for (j = e->first; j < e->first + e->count; ++j)
    if (!strcmp(c->enum_members[j].name, name))
      return j;
  return -1;
}
int enum_string(Compiler *c, int member) {
  EnumMember *m = &c->enum_members[member];
  size_t size = strlen(m->name) + 1;
  if (!m->string) {
    if (c->nstrings + size > MAX_STRINGS)
      fail(c, m->token, "string arena exhausted");
    m->string = c->nstrings + 1;
    memcpy(c->strings + c->nstrings, m->name, size);
    c->nstrings += (int)size;
  }
  return m->string - 1;
}
void enum_constant(Compiler *c, int node, int type, int member, int name) {
  Node *n = &c->nodes[node];
  EnumMember *m = &c->enum_members[member];
  n->kind = name ? N_STRING : N_CONST;
  n->type = name ? TY_STR : type;
  n->value = name ? enum_string(c, m->canonical) : m->value;
  n->d = name ? (int)strlen(c->enum_members[m->canonical].name) : 0;
  n->symbol = -1;
  n->a = n->b = n->c = 0;
}
static int qualified_member(Compiler *c, int node, int *type) {
  char name[192], *dot;
  int module, j;
  if (!named_tree(c, node))
    return -1;
  resolve_name(c, node, &module, name, sizeof(name));
  dot = strrchr(name, '.');
  if (!dot)
    return -1;
  *dot++ = 0;
  *type = enum_named(c, module, name);
  if (!*type)
    return -1;
  EnumLayout *e = &c->enums[(*type - TY_ENUM_BASE) / 4];
  j = member_named(c, e, dot);
  if (j < 0)
    fail(c, c->nodes[node].token, "IntEnum '%s' has no member '%s'", name, dot);
  return j;
}
/* Evaluate member definitions without exposing their temporary class namespace
   as module variables. Copies let the ordinary optimizer handle integer rules.
 */
static long constant(Compiler *c, int node, EnumLayout *local, int limit,
                     int depth) {
  Node *n = &c->nodes[node];
  char name[192];
  int j, module, type = 0;
  if (depth > MAX_DEPTH)
    fail(c, n->token, "enum constant expression is too deeply nested");
  if (n->kind == N_CONST)
    return n->value;
  if (n->kind == N_ATTR) {
    char property[64];
    int member;
    token_text(c, n->token, property, sizeof(property));
    if (!strcmp(property, "value") &&
        (member = qualified_member(c, n->a, &type)) >= 0) {
      if (!c->enums[(type - TY_ENUM_BASE) / 4].ready)
        fail(c, n->token, "IntEnum member used before its class definition");
      return c->enum_members[member].value;
    }
  }
  if (n->kind == N_NAME || n->kind == N_ATTR) {
    resolve_name(c, node, &module, name, sizeof(name));
    if (local && n->kind == N_NAME && (j = member_named(c, local, name)) >= 0)
      return c->enum_members[j].value;
    j = qualified_member(c, node, &type);
    if (j >= 0) {
      EnumLayout *e = &c->enums[(type - TY_ENUM_BASE) / 4];
      if (!e->ready)
        fail(c, n->token, "IntEnum member used before its class definition");
      return c->enum_members[j].value;
    }
    for (j = c->root; j && j != limit; j = c->nodes[j].next) {
      Node *definition = &c->nodes[j];
      char other[64];
      if (definition->kind != N_DECLCONST ||
          c->tokens[definition->token].module != module)
        continue;
      token_text(c, c->nodes[definition->a].token, other, sizeof(other));
      if (!strcmp(name, other))
        return constant(c, definition->b, NULL, j, depth + 1);
    }
    fail(c, n->token,
         "enum value requires an earlier member or Final constant");
  }
  if (n->kind == N_BINARY || n->kind == N_UNARY) {
    long left = constant(c, n->a, local, limit, depth + 1);
    if (n->kind == N_BINARY &&
        ((n->op == OP_AND && !left) || (n->op == OP_OR && left)))
      return left;
    int copy = node_new(c, n->kind, n->token);
    Node *out = &c->nodes[copy];
    out->op = n->op;
    out->a = node_new(c, N_CONST, n->token);
    c->nodes[out->a].value = left;
    if (n->kind == N_BINARY) {
      out->b = node_new(c, N_CONST, n->token);
      c->nodes[out->b].value = constant(c, n->b, local, limit, depth + 1);
    }
    fold_expression(c, copy);
    if (out->kind == N_CONST)
      return out->value;
  }
  if (n->kind == N_CALL) {
    long value;
    resolve_name(c, n->a, &module, name, sizeof(name));
    if (!strcmp(name, "enum.auto")) {
      if (!local || n->b || depth)
        fail(c, n->token,
             "auto() must be a complete member initializer with no arguments");
      value = 0;
      for (j = local->first; j < local->first + local->count; ++j)
        if (j == local->first || c->enum_members[j].value > value)
          value = c->enum_members[j].value;
      return value + 1;
    }
    type = library_type(c, n->a);
    if (n->b && !c->nodes[n->b].next &&
        (type == TY_BOOL || (type >= TY_U8 && type <= TY_I16) ||
         enum_type(type))) {
      value = constant(c, n->b, local, limit, depth + 1);
      if (!strcmp(name, "int"))
        return value;
      if (enum_type(type)) {
        if (!c->enums[(type - TY_ENUM_BASE) / 4].ready ||
            enum_value_member(c, type, value) < 0)
          fail(c, n->token, "invalid IntEnum constant value");
        return value;
      }
      if (type == TY_BOOL)
        return value != 0;
      value &= type_size(type) == 1 ? 255 : 65535;
      if (type_signed(type) && value >= (type_size(type) == 1 ? 128 : 32768))
        value -= type_size(type) == 1 ? 256 : 65536;
      return value;
    }
  }
  fail(c, n->token, "enum members require compile-time integer values");
  return 0;
}
static void require_ready(Compiler *c, int type, int token, int function) {
  if (!function && !c->enums[(type - TY_ENUM_BASE) / 4].ready)
    fail(c, token, "IntEnum used before its class definition");
}
static void expression(Compiler *c, int node, int function, int target,
                       int depth) {
  Node *n = &c->nodes[node];
  int type = 0, member;
  char name[64];
  if (!node)
    return;
  if (depth > MAX_DEPTH)
    fail(c, n->token, "expression nesting is too deep");
  if (n->kind == N_NAME || n->kind == N_ATTR) {
    member = qualified_member(c, node, &type);
    if (member >= 0) {
      require_ready(c, type, n->token, function);
      if (target)
        fail(c, n->token, "IntEnum members are read-only");
      enum_constant(c, node, type, member, 0);
      return;
    }
    type = enum_class(c, node);
    if (type)
      require_ready(c, type, n->token, function);
    if (n->kind == N_ATTR) {
      expression(c, n->a, function, 0, depth + 1);
      Node *base = &c->nodes[n->a];
      if (base->kind == N_CONST && enum_type(base->type)) {
        token_text(c, n->token, name, sizeof(name));
        if (strcmp(name, "name") && strcmp(name, "value"))
          fail(c, n->token, "IntEnum values provide name and value properties");
        if (target)
          fail(c, n->token, "IntEnum properties are read-only");
        member = enum_value_member(c, base->type, base->value);
        enum_constant(c, node,
                      !strcmp(name, "value") ? integer_type(base->type)
                                             : base->type,
                      member, !strcmp(name, "name"));
      }
    }
    return;
  }
  if (n->kind == N_CALL) {
    type = enum_class(c, n->a);
    if (type) {
      require_ready(c, type, n->token, function);
      if (!n->b || c->nodes[n->b].next)
        fail(c, n->token, "IntEnum conversion expects one integer value");
      expression(c, n->b, function, 0, depth + 1);
      n->kind = N_ENUM_LOOKUP;
      n->a = n->b;
      n->b = 0;
      n->c = n->type = type;
      if (c->nodes[n->a].kind == N_CONST) {
        member = enum_value_member(c, type, c->nodes[n->a].value);
        if (member < 0)
          fail(c, n->token, "value is not a member of IntEnum '%s'",
               c->enums[(type - TY_ENUM_BASE) / 4].name);
        enum_constant(c, node, type, member, 0);
        n->c = 1; /* Preserve constructor origin for expression statements. */
      }
      return;
    }
    if (c->nodes[n->a].kind == N_ATTR)
      expression(c, c->nodes[n->a].a, function, 0, depth + 1);
    for (member = n->b; member; member = c->nodes[member].next)
      expression(c, member, function, 0, depth + 1);
    /* len(Enum) counts distinct members, excluding aliases. */
    if (named_tree(c, n->a)) {
      int module;
      char full[192];
      resolve_name(c, n->a, &module, full, sizeof(full));
      if (!strcmp(full, "len") && n->b && !c->nodes[n->b].next &&
          (type = enum_class(c, n->b))) {
        EnumLayout *e = &c->enums[(type - TY_ENUM_BASE) / 4];
        long count = 0;
        for (member = e->first; member < e->first + e->count; ++member)
          count += c->enum_members[member].canonical == member;
        n->kind = N_CONST;
        n->value = count;
        n->type = TY_U16;
        n->c = 1;
        n->a = n->b = 0;
      }
    }
    return;
  }
  if (n->kind == N_INDEX && (type = enum_class(c, n->a))) {
    require_ready(c, type, n->token, function);
    if (target)
      fail(c, n->token, "IntEnum members are read-only");
    expression(c, n->b, function, 0, depth + 1);
    n->kind = N_ENUM_LOOKUP;
    n->op = 2; /* Lookup a member by its name, including aliases. */
    n->a = n->b;
    n->b = 0;
    n->c = n->type = type;
    if (c->nodes[n->a].kind == N_STRING) {
      member = member_named(c, &c->enums[(type - TY_ENUM_BASE) / 4],
                            c->strings + c->nodes[n->a].value);
      if (member < 0)
        fail(c, n->token, "unknown IntEnum member name");
      enum_constant(c, node, type, member, 0);
    }
    return;
  }
  if (n->kind == N_BINARY || n->kind == N_UNARY || n->kind == N_INDEX ||
      n->kind == N_SLICE || n->kind == N_FLOAT_CAST) {
    expression(c, n->a, function, target, depth + 1);
    expression(c, n->b, function, 0, depth + 1);
    if (n->kind == N_SLICE) {
      expression(c, n->c, function, 0, depth + 1);
      expression(c, n->d, function, 0, depth + 1);
    }
  } else if (n->kind == N_LIST)
    for (member = n->a; member; member = c->nodes[member].next)
      expression(c, member, function, 0, depth + 1);
}
static void block(Compiler *c, int node, int function, int depth) {
  int j, type;
  if (depth > MAX_DEPTH)
    fail(c, c->nodes[node].token, "statement nesting is too deep");
  for (; node; node = c->nodes[node].next) {
    Node *n = &c->nodes[node];
    switch (n->kind) {
    case N_ENUMDEF:
      c->enums[(n->type - TY_ENUM_BASE) / 4].ready = 1;
      break;
    case N_FUNC:
      block(c, n->b, 1, depth + 1);
      break;
    case N_CLASS:
      block(c, n->a, function, depth + 1);
      break;
    case N_ASSIGN:
    case N_DECLCONST:
      expression(c, n->a, function, 1, 0);
      expression(c, n->b, function, 0, 0);
      break;
    case N_IF:
    case N_WHILE:
      expression(c, n->a, function, 0, 0);
      block(c, n->b, function, depth + 1);
      block(c, n->kind == N_IF ? n->c : n->else_body, function, depth + 1);
      break;
    case N_EACH:
      type = enum_class(c, n->b);
      if (type) {
        EnumLayout *e = &c->enums[(type - TY_ENUM_BASE) / 4];
        Node *values = &c->nodes[n->b];
        int tail = 0;
        require_ready(c, type, n->token, function);
        values->kind = N_LIST;
        values->type = TY_LIST;
        values->a = values->d = 0;
        values->c = type;
        for (j = e->first; j < e->first + e->count; ++j)
          if (c->enum_members[j].canonical == j) {
            int item = node_new(c, N_CONST, n->token);
            enum_constant(c, item, type, j, 0);
            if (tail)
              c->nodes[tail].next = item;
            else
              values->a = item;
            tail = item;
            ++values->d;
          }
      }
      expression(c, n->b, function, 0, 0);
      block(c, (int)n->value, function, depth + 1);
      block(c, n->else_body, function, depth + 1);
      break;
    case N_FOR:
      expression(c, n->b, function, 0, 0);
      expression(c, n->c, function, 0, 0);
      expression(c, n->d, function, 0, 0);
      block(c, (int)n->value, function, depth + 1);
      block(c, n->else_body, function, depth + 1);
      break;
    case N_RETURN:
    case N_EXPR:
    case N_ASSERT:
      expression(c, n->a, function, 0, 0);
      if (n->kind == N_ASSERT)
        expression(c, n->b, function, 0, 0);
      break;
    default:
      break;
    }
  }
}
void prepare_enums(Compiler *c) {
  int i, j, module;
  char name[192];
  for (i = c->root; i; i = c->nodes[i].next)
    if (c->nodes[i].kind == N_ENUMDEF) {
      Node *n = &c->nodes[i];
      EnumLayout *e;
      resolve_name(c, n->c, &module, name, sizeof(name));
      if (module != -1 || strcmp(name, "enum.IntEnum"))
        fail(c, n->token,
             "class inheritance is unsupported except enum.IntEnum");
      check_binding(c, n->token);
      token_text(c, n->token, name, sizeof(name));
      module = c->tokens[n->token].module;
      if (enum_named(c, module, name))
        fail(c, n->token, "duplicate IntEnum '%s'", name);
      if (c->nenums == MAX_ENUMS)
        fail(c, n->token, "enum arena exhausted (limit %d)", MAX_ENUMS);
      e = &c->enums[c->nenums];
      strcpy(e->name, name);
      e->node = i;
      e->module = module;
      e->type = TY_ENUM_BASE + c->nenums++ * 4;
    }
  for (i = 0; i < c->nenums; ++i) {
    EnumLayout *e = &c->enums[i];
    Node *n = &c->nodes[e->node];
    long low = 0, high = 0;
    e->first = c->nenum_members;
    for (j = n->a; j; j = c->nodes[j].next) {
      Node *definition = &c->nodes[j];
      EnumMember *m;
      long value;
      int alias;
      if (definition->kind == N_PASS)
        continue;
      if (definition->kind != N_ASSIGN || definition->op || !definition->b ||
          c->nodes[definition->a].kind != N_NAME || definition->type)
        fail(c, definition->token,
             "IntEnum bodies require unannotated integer member assignments");
      token_text(c, c->nodes[definition->a].token, name, sizeof(name));
      if (name[0] == '_' || !strcmp(name, "name") || !strcmp(name, "value"))
        fail(c, definition->token, "reserved IntEnum member name '%s'", name);
      if (member_named(c, e, name) >= 0)
        fail(c, definition->token, "duplicate IntEnum member '%s'", name);
      value = constant(c, definition->b, e, e->node, 0);
      if (value < -32768 || value > 65535)
        fail(c, definition->token,
             "enum value exceeds the 16-bit integer range");
      if (!e->count || value < low)
        low = value;
      if (!e->count || value > high)
        high = value;
      alias = enum_value_member(c, e->type, value);
      if (c->nenum_members == MAX_ENUM_MEMBERS)
        fail(c, definition->token, "enum member arena exhausted (limit %d)",
             MAX_ENUM_MEMBERS);
      m = &c->enum_members[c->nenum_members];
      strcpy(m->name, name);
      m->value = value;
      m->token = definition->token;
      m->canonical = alias < 0 ? c->nenum_members : alias;
      ++c->nenum_members;
      ++e->count;
    }
    if (!e->count)
      fail(c, n->token, "IntEnum requires at least one member");
    if (low < 0 && high > 32767)
      fail(c, n->token, "enum values need a common 16-bit signedness");
    e->type += (low < 0 ? (low >= -128 && high <= 127 ? TY_I8 : TY_I16)
                        : (high <= 255 ? TY_U8 : TY_U16)) -
               TY_U8;
    n->type = e->type;
    n->a = n->c = 0;
    e->ready = 1;
  }
  for (i = 0; i < c->nenums; ++i)
    c->enums[i].ready = 0;
  block(c, c->root, 0, 0);
}

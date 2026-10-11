#include "pyc64.h"

int type_size(int t) {
  t = integer_type(t);
  if (t == TY_FLOAT)
    return 5;
  return t == TY_U16 || t == TY_I16 || t == TY_STR || t == TY_BYTES ||
                 object_type(t)
             ? 2
             : 1;
}
int symbol_size(const Symbol *s) {
  return s->reference           ? (object_type(s->type) ? 2 : 4)
         : object_type(s->type) ? s->count
         : s->type == TY_ARRAY || s->type == TY_LIST
             ? s->count * type_size(s->element)
             : type_size(s->type);
}
int type_signed(int t) {
  t = integer_type(t);
  return t == TY_I8 || t == TY_I16;
}
const char *type_name(int t) {
  static const char *names[] = {"unknown", "bool", "u8",    "i8",
                                "u16",     "i16",  "str",   "array",
                                "None",    "list", "float", "bytes"};
  t = integer_type(t);
  return object_type(t) ? "object" : names[t];
}
static int numeric(int t) {
  return (t >= TY_BOOL && t <= TY_I16) || enum_type(t);
}
static int number(int t) { return numeric(t) || t == TY_FLOAT; }
static void constant_character(Compiler *c, Node *n, unsigned char ch) {
  int j;
  for (j = 0; j + 1 < c->nstrings; ++j)
    if ((!j || !c->strings[j - 1]) && (unsigned char)c->strings[j] == ch &&
        !c->strings[j + 1])
      break;
  if (j + 1 >= c->nstrings) {
    if (c->nstrings + 2 > MAX_STRINGS)
      fail(c, n->token, "string arena exhausted");
    j = c->nstrings;
    c->strings[c->nstrings++] = (char)ch;
    c->strings[c->nstrings++] = 0;
  }
  n->kind = N_STRING;
  n->type = TY_STR;
  n->value = j;
  n->d = 1;
  n->a = n->b = 0;
}
static int fits(long v, int t) {
  switch (t) {
  case TY_BOOL:
    return v == 0 || v == 1;
  case TY_U8:
    return v >= 0 && v <= 255;
  case TY_I8:
    return v >= -128 && v <= 127;
  case TY_U16:
    return v >= 0 && v <= 65535;
  case TY_I16:
    return v >= -32768 && v <= 32767;
  default:
    return 0;
  }
}
static int infer(long v) {
  return v < 0 ? (v >= -128 ? TY_I8 : TY_I16) : (v <= 255 ? TY_U8 : TY_U16);
}
static int find_symbol(Compiler *c, const char *s, int scope, int module) {
  int i;
  for (i = 0; i < c->nsymbols; ++i)
    if (c->symbols[i].scope == scope && c->symbols[i].module == module &&
        !strcmp(c->symbols[i].name, s))
      return i;
  return -1;
}
static int add_symbol(Compiler *c, int tok, int type, int element, int count) {
  int i = c->nsymbols++;
  Symbol *s;
  check_binding(c, tok);
  if (i == MAX_SYMBOLS)
    fail(c, tok, "symbol arena exhausted");
  s = &c->symbols[i];
  token_text(c, tok, s->name, sizeof(s->name));
  if (class_named(c, c->tokens[tok].module, s->name) ||
      enum_named(c, c->tokens[tok].module, s->name))
    fail(c, tok, "declaration conflicts with class '%s'", s->name);
  s->type = type;
  s->element = element;
  s->count = count;
  s->scope = c->scope;
  s->initialized = 1;
  s->module = c->tokens[tok].module;
  if (!c->scope) {
    int f;
    for (f = 0; f < c->nfunctions; ++f)
      if (c->functions[f].module == s->module &&
          !strcmp(c->functions[f].name, s->name))
        fail(c, tok, "variable conflicts with function '%s'", s->name);
  }
  if (find_symbol(c, s->name, c->scope, s->module) != i)
    fail(c, tok, "duplicate declaration '%s'", s->name);
  return i;
}
static int hardware(Compiler *c, int i) {
  char s[192];
  Node *n = &c->nodes[i];
  int port = 2, bit = -1, module;
  const char *p;
  resolve_name(c, i, &module, s, sizeof(s));
  if (module >= 0)
    return 0;
  if (!strcmp(s, "c64.video.screen.border"))
    n->value = 0xd020;
  else if (!strcmp(s, "c64.video.screen.background"))
    n->value = 0xd021;
  else if (!strncmp(s, "c64.input.joystick.", 19)) {
    p = s + 19;
    if (!strncmp(p, "port1.", 6)) {
      port = 1;
      p += 6;
    } else if (!strncmp(p, "port2.", 6))
      p += 6;
    if (!strcmp(p, "up"))
      bit = 0;
    else if (!strcmp(p, "down"))
      bit = 1;
    else if (!strcmp(p, "left"))
      bit = 2;
    else if (!strcmp(p, "right"))
      bit = 3;
    else if (!strcmp(p, "fire"))
      bit = 4;
    if (bit < 0)
      fail(c, n->token, "unknown joystick property");
    n->value = port == 2 ? 0xdc00 : 0xdc01;
    n->d = 1 << bit;
  } else
    return 0;
  n->kind = N_HW;
  n->type = bit < 0 ? TY_U8 : TY_BOOL;
  return 1;
}
static int expr(Compiler *c, int i, int expected);
static void object_fields(Compiler *c, int parent, int ready) {
  Symbol *object = &c->symbols[parent];
  ClassLayout *layout = &c->classes[object->type - TY_CLASS_BASE];
  int field;
  for (field = 0; field < layout->nfields; ++field) {
    Symbol *s;
    char name[64];
    Node *definition = &c->nodes[layout->fields[field]];
    if (c->nsymbols == MAX_SYMBOLS)
      fail(c, definition->token, "symbol arena exhausted");
    s = &c->symbols[c->nsymbols++];
    token_text(c, definition->token, name, sizeof(name));
    if (strlen(object->name) + strlen(name) + 2 > sizeof(s->name))
      fail(c, definition->token, "object field name is too long");
    strcpy(s->name, object->name);
    strcat(s->name, ".");
    strcat(s->name, name);
    s->parent = parent + 1;
    s->type = definition->type;
    s->scope = object->scope;
    s->module = object->module;
    s->count = field;
    s->value = layout->offsets[field];
    s->initialized = ready || definition->b != 0;
  }
}
static void object_ready(Compiler *c, int object, int token) {
  int i;
  for (i = 0; i < c->nsymbols; ++i)
    if (c->symbols[i].parent == object + 1 && !c->symbols[i].initialized)
      fail(c, token, "field '%s' may be read before initialization",
           c->symbols[i].name);
}
static int field_receiver(Compiler *c, int i) {
  Node *n = &c->nodes[i];
  int parent, module;
  char name[192];
  if (module_namespace(c, n->a))
    return -1;
  resolve_name(c, n->a, &module, name, sizeof(name));
  parent = find_symbol(c, name, c->scope, module);
  if (parent < 0)
    parent = find_symbol(c, name, 0, module);
  if (parent < 0 || !object_type(c->symbols[parent].type))
    return -1;
  expr(c, n->a, 0);
  if (c->nodes[n->a].kind != N_NAME)
    fail(c, n->token, "field access requires a named object");
  return c->nodes[n->a].symbol;
}
static int accessor_function(Compiler *c, int node) {
  int i;
  for (i = 0; i < c->nfunctions; ++i)
    if (c->functions[i].node == node)
      return i;
  fail(c, c->nodes[node].token, "internal error: missing property function");
  return -1;
}
static int object_argument(Compiler *c, int parent, int token) {
  int argument = node_new(c, N_NAME, token);
  c->nodes[argument].symbol = parent;
  c->nodes[argument].type = c->symbols[parent].type;
  return argument;
}
static int field_expression(Compiler *c, int i, int writing) {
  Node *n = &c->nodes[i];
  int parent = field_receiver(c, i), type, field, j, property;
  if (parent < 0)
    return 0;
  type = c->symbols[parent].type;
  property = class_property(c, type, n->token, writing);
  if (property && !writing) {
    j = accessor_function(c, property);
    object_ready(c, parent, n->token);
    n->kind = N_PROPERTY_CALL;
    n->symbol = j;
    n->type = c->functions[j].type;
    n->a = 0;
    n->b = object_argument(c, parent, n->token);
    return n->type;
  }
  field = class_field(c, type, n->token);
  for (j = 0; j < c->nsymbols; ++j)
    if (c->symbols[j].parent == parent + 1 && c->symbols[j].count == field)
      break;
  if (j == c->nsymbols)
    fail(c, n->token, "internal error: missing object field");
  if (!writing && !c->symbols[j].initialized)
    fail(c, n->token, "field '%s' may be read before initialization",
         c->symbols[j].name);
  n->kind = N_FIELD;
  n->symbol = j;
  n->type = c->symbols[j].type;
  return n->type;
}
static void parameter_extent(Compiler *c, Symbol *s) {
  int scope;
  if (!s->reference || s->type != TY_ARRAY || !s->value || s->count >= 0)
    return;
  scope = c->scope;
  c->scope = 0; /* Annotation constants belong to the defining module. */
  ++c->constant_context;
  expr(c, (int)s->value, 0);
  --c->constant_context;
  c->scope = scope;
  if (c->nodes[s->value].kind != N_CONST || c->nodes[s->value].value < 1 ||
      c->nodes[s->value].value > 16384)
    fail(c, c->nodes[s->value].token,
         "array parameter size must be a compile-time integer in 1..16384");
  s->count = (int)c->nodes[s->value].value;
}
static int local_binding(Compiler *c, int i, const char *name) {
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    char binding[64];
    if ((n->kind == N_ASSIGN || n->kind == N_FOR || n->kind == N_EACH) &&
        c->nodes[n->a].kind == N_NAME) {
      token_text(c, c->nodes[n->a].token, binding, sizeof(binding));
      if (!strcmp(name, binding))
        return 1;
    }
    if ((n->kind == N_IF || n->kind == N_WHILE) &&
        (local_binding(c, n->b, name) ||
         (n->kind == N_IF && local_binding(c, n->c, name))))
      return 1;
    if ((n->kind == N_FOR || n->kind == N_EACH) &&
        local_binding(c, (int)n->value, name))
      return 1;
    if (local_binding(c, n->else_body, name))
      return 1;
  }
  return 0;
}
static void assign_type(Compiler *c, int token, int src, int dest) {
  if (src == dest)
    return;
  if (enum_type(dest))
    fail(c, token,
         "assignment requires a member of IntEnum '%s'; use %s(value)",
         c->enums[(dest - TY_ENUM_BASE) / 4].name,
         c->enums[(dest - TY_ENUM_BASE) / 4].name);
  src = integer_type(src);
  if (numeric(src) && dest == TY_FLOAT)
    return;
  if (numeric(src) && numeric(dest) && dest != TY_BOOL &&
      type_size(src) <= type_size(dest) &&
      !(type_signed(src) && !type_signed(dest)) &&
      !(src == TY_U16 && dest == TY_I16) && !(src == TY_U8 && dest == TY_I8))
    return;
  fail(c, token, "cannot assign %s to %s; use an explicit integer cast",
       type_name(src), type_name(dest));
}
static void assignable(Compiler *c, int i, int dest) {
  Node *n = &c->nodes[i];
  int src = expr(c, i, dest);
  if (dest == TY_FLOAT && numeric(src)) {
    if (n->kind == N_CONST) {
      n->kind = N_FLOAT;
      n->real = (double)n->value;
    } else {
      int child = node_new(c, N_FLOAT_CAST, n->token), next = n->next;
      c->nodes[child] = *n;
      c->nodes[child].next = 0;
      memset(n, 0, sizeof(*n));
      n->kind = N_FLOAT_CAST;
      n->a = child;
      n->next = next;
      n->token = c->nodes[child].token;
      n->symbol = -1;
    }
    n->type = TY_FLOAT;
    return;
  }
  if (n->kind == N_CONST && numeric(dest) && !enum_type(dest)) {
    if (!fits(n->value, dest))
      fail(c, n->token, "value %ld does not fit in %s; use a wider type",
           n->value, type_name(dest));
    n->type = dest;
    return;
  }
  assign_type(c, n->token, src, dest);
}
static int expr(Compiler *c, int i, int expected) {
  Node *n = &c->nodes[i];
  int a, b, j;
  if (++c->depth > MAX_DEPTH)
    fail(c, n->token, "expression nesting exceeds %d", MAX_DEPTH);
  switch (n->kind) {
  case N_CONST:
    if (n->value < -32768 || n->value > 65535)
      fail(c, n->token, "integer constant is outside the 16-bit range");
    if (!n->type)
      n->type = infer(n->value);
    break;
  case N_STRING:
    n->type = TY_STR;
    break;
  case N_BYTES:
    n->type = TY_BYTES;
    break;
  case N_FLOAT: {
    unsigned char packed[5];
    float_pack(c, n->token, n->real, packed);
    n->type = TY_FLOAT;
    break;
  }
  case N_FLOAT_CAST:
    expr(c, n->a, 0);
    n->type = TY_FLOAT;
    break;
  case N_LIST: {
    int element = n->c;
    for (j = n->a; j; j = c->nodes[j].next) {
      a = expr(c, j, 0);
      if (a != TY_STR && a != TY_BYTES && !number(a))
        fail(c, c->nodes[j].token,
             "list elements must be integers, bool or str");
      if (!element)
        element = a;
      else if (element != a) {
        if ((element == TY_FLOAT && numeric(a)) ||
            (a == TY_FLOAT && numeric(element))) {
          element = TY_FLOAT;
          continue;
        }
        if (enum_type(element) || enum_type(a) || !numeric(element) ||
            !numeric(a) || element == TY_BOOL || a == TY_BOOL ||
            (element == TY_U16 && type_signed(a)) ||
            (a == TY_U16 && type_signed(element)))
          fail(c, c->nodes[j].token,
               "list elements require a consistent scalar type");
        element = type_signed(element) != type_signed(a) ? TY_I16
                  : type_size(element) >= type_size(a)   ? element
                                                         : a;
      }
    }
    n->type = TY_LIST;
    n->c = element;
    for (j = n->a; j; j = c->nodes[j].next)
      assignable(c, j, element);
    break;
  }
  case N_NAME:
  case N_ATTR: {
    char name[192];
    int module, owner = c->tokens[n->token].module;
    if (n->symbol >= 0)
      break;
    if (n->kind == N_ATTR) {
      Node *base = &c->nodes[n->a];
      int candidate = base->kind != N_NAME && base->kind != N_ATTR;
      if (!candidate && !module_namespace(c, n->a)) {
        resolve_name(c, n->a, &module, name, sizeof(name));
        j = find_symbol(c, name, c->scope, module);
        if (j < 0)
          j = find_symbol(c, name, 0, module);
        candidate = j >= 0 && enum_type(c->symbols[j].type);
        if (!candidate && base->kind == N_ATTR)
          candidate = field_receiver(c, n->a) >= 0;
      }
      if (candidate && enum_type(a = expr(c, n->a, 0))) {
        token_text(c, n->token, name, sizeof(name));
        if (strcmp(name, "name") && strcmp(name, "value"))
          fail(c, n->token, "IntEnum values provide name and value properties");
        n->kind = !strcmp(name, "value") ? N_ENUM_VALUE : N_ENUM_LOOKUP;
        n->op = 1;
        n->c = a;
        n->type = n->kind == N_ENUM_VALUE ? integer_type(a) : TY_STR;
        if (n->kind == N_ENUM_LOOKUP) {
          EnumLayout *e = &c->enums[(a - TY_ENUM_BASE) / 4];
          for (j = e->first; j < e->first + e->count; ++j)
            enum_string(c, c->enum_members[j].canonical);
        }
        break;
      }
    }
    if (n->kind == N_ATTR && field_expression(c, i, 0))
      break;
    if (hardware(c, i))
      break;
    resolve_name(c, i, &module, name, sizeof(name));
    if (!*name)
      fail(c, n->token, "module namespace requires a member name");
    j = module == owner ? find_symbol(c, name, c->scope, module) : -1;
    if (j < 0)
      j = find_symbol(c, name, 0, module);
    if (j < 0)
      fail(c, n->token, "unknown variable '%s'", name);
    if (!c->symbols[j].initialized ||
        (c->scope && module == owner && !c->symbols[j].scope &&
         local_binding(c, c->nodes[c->functions[c->scope - 1].node].b, name)))
      fail(c, n->token, "variable '%s' may be read before initialization",
           name);
    n->symbol = j;
    n->type = c->symbols[j].type;
    if (c->symbols[j].constant) {
      n->kind = n->type == TY_BYTES   ? N_BYTES
                : n->type == TY_STR   ? N_STRING
                : n->type == TY_FLOAT ? N_FLOAT
                                      : N_CONST;
      n->value = c->symbols[j].value;
      n->real = c->symbols[j].real;
      n->d = c->symbols[j].count;
      n->a = n->b = 0;
    } else
      n->kind = N_NAME;
    break;
  }
  case N_HW:
    break;
  case N_FIELD:
    if (!c->symbols[n->symbol].initialized)
      fail(c, n->token, "field '%s' may be read before initialization",
           c->symbols[n->symbol].name);
    break;
  case N_PROPERTY_CALL:
    break; /* Already resolved, with an independently captured receiver node. */
  case N_ENUM_VALUE:
    a = expr(c, n->a, 0);
    n->type = integer_type(a);
    break;
  case N_ENUM_LOOKUP:
    a = expr(c, n->a, 0);
    if (n->op == 2 ? a != TY_STR : !numeric(a))
      fail(c, n->token,
           n->op == 2 ? "IntEnum name lookup requires a string"
                      : "IntEnum conversion requires an integer value");
    n->type = n->op == 1 ? TY_STR : n->c;
    if (n->op) {
      EnumLayout *e = &c->enums[(n->c - TY_ENUM_BASE) / 4];
      for (j = e->first; j < e->first + e->count; ++j)
        enum_string(c, n->op == 2 ? j : c->enum_members[j].canonical);
    }
    if (!n->op && c->nodes[n->a].kind == N_CONST) {
      j = enum_value_member(c, n->c, c->nodes[n->a].value);
      if (j < 0)
        fail(c, n->token, "value is not a member of IntEnum '%s'",
             c->enums[(n->c - TY_ENUM_BASE) / 4].name);
      enum_constant(c, i, n->c, j, 0);
    }
    break;
  case N_SLICE: {
    int *bounds[3] = {&n->b, &n->c, &n->d}, k, literals = 1;
    a = expr(c, n->a, 0);
    if (a != TY_BYTES)
      fail(c, n->token, "slicing currently requires bytes");
    n->type = TY_BYTES;
    n->op = 0;
    for (k = 0; k < 3; ++k) {
      int bound = *bounds[k];
      if (bound && c->nodes[bound].kind == N_NONE)
        bound = *bounds[k] = 0;
      if (!bound) {
        if (k < 2)
          n->op |= 1 << k;
        continue;
      }
      b = expr(c, bound, 0);
      if (!numeric(b))
        fail(c, n->token,
             "slice bounds and step require integers or literal None");
      if (type_signed(b))
        n->op |= 4 << k;
      if (c->nodes[bound].kind != N_CONST)
        literals = 0;
    }
    if (n->d && c->nodes[n->d].kind == N_CONST && !c->nodes[n->d].value)
      fail(c, n->token, "slice step cannot be zero");
    if (literals && c->nodes[n->a].kind == N_BYTES)
      bytes_slice_literal(c, i);
    else if (!n->c &&
             (!n->b ||
              (c->nodes[n->b].kind == N_CONST && !c->nodes[n->b].value)) &&
             (!n->d ||
              (c->nodes[n->d].kind == N_CONST && c->nodes[n->d].value == 1))) {
      int next = n->next, token = n->token;
      *n = c->nodes[n->a]; /* Pure full slices need no allocator or helper. */
      n->next = next;
      n->token = token;
    }
    break;
  }
  case N_INDEX:
    a = expr(c, n->a, 0);
    b = expr(c, n->b, 0);
    if (a == TY_STR || a == TY_BYTES) {
      if (!numeric(b))
        fail(c, n->token, "string index must be an integer");
      n->type = a == TY_BYTES ? TY_U8 : TY_STR;
      if ((c->nodes[n->a].kind == N_STRING || c->nodes[n->a].kind == N_BYTES) &&
          c->nodes[n->b].kind == N_CONST) {
        Node *source = &c->nodes[n->a];
        long index = c->nodes[n->b].value;
        if (index < 0)
          index += source->d;
        if (index < 0 || index >= source->d)
          fail(c, n->token, "string index is out of bounds");
        if (a == TY_BYTES) {
          n->kind = N_CONST;
          n->value = c->bytes[source->value + index];
          n->a = n->b = 0;
          n->d = 0;
        } else
          constant_character(c, n,
                             (unsigned char)c->strings[source->value + index]);
      }
      break;
    }
    if ((a != TY_ARRAY && a != TY_LIST) || !numeric(b))
      fail(c, n->token,
           "indexing requires a fixed array/list and an integer index");
    j = c->nodes[n->a].symbol;
    if (j < 0)
      fail(c, n->token,
           "assign the list literal to a variable before indexing it");
    n->symbol = j;
    n->type = c->symbols[j].element;
    if (c->nodes[n->b].kind == N_CONST &&
        ((a == TY_ARRAY && c->nodes[n->b].value < 0) ||
         (c->symbols[j].count >= 0 &&
          (c->nodes[n->b].value < (a == TY_LIST ? -c->symbols[j].count : 0) ||
           c->nodes[n->b].value >= c->symbols[j].count))))
      fail(c, n->token, "%s index is out of bounds",
           a == TY_LIST ? "list" : "array");
    break;
  case N_UNARY:
    a = expr(c, n->a, expected);
    if (a == TY_FLOAT) {
      if (n->op != '+' && n->op != '-' && n->op != OP_NOT)
        fail(c, n->token, "float does not support this unary operator");
      n->type = n->op == OP_NOT ? TY_BOOL : TY_FLOAT;
      if (c->nodes[n->a].kind == N_FLOAT && n->op != OP_NOT) {
        n->real = c->nodes[n->a].real * (n->op == '-' ? -1 : 1);
        n->kind = N_FLOAT;
        n->a = 0;
      }
      break;
    }
    fold_expression(c, i);
    if (n->kind == N_CONST) {
      if (!n->type)
        n->type = infer(n->value);
      break;
    }
    if (n->op == OP_NOT && (a == TY_STR || a == TY_BYTES)) {
      n->type = TY_BOOL;
      break;
    }
    if (!numeric(a))
      fail(c, n->token, "unary operator requires an integer");
    n->type = n->op == OP_NOT ? TY_BOOL
              : enum_type(a) && (n->op == '-' || n->op == '~')
                  ? TY_I16
                  : integer_type(a);
    if (n->type == TY_BOOL && n->op != OP_NOT)
      n->type = n->op == '-' ? TY_I8 : TY_U8;
    break;
  case N_BINARY:
    if (n->op == OP_IN || n->op == OP_NOTIN) {
      a = expr(c, n->a, 0);
      b = expr(c, n->b, 0);
      if (b != TY_BYTES || (a != TY_BYTES && !numeric(a)))
        fail(c, n->token,
             "bytes membership requires a bytes container and bytes or an "
             "integer byte");
      if (a != TY_BYTES && c->nodes[n->a].kind == N_CONST &&
          (c->nodes[n->a].value < 0 || c->nodes[n->a].value > 255))
        fail(c, n->token, "integer byte needles must be in 0..255");
      n->type = TY_BOOL;
      n->c = a != TY_BYTES;
      fold_expression(c, i);
      break;
    }
    a = expr(c, n->a, expected);
    if (c->nodes[n->a].kind == N_CONST &&
        ((n->op == OP_AND && !c->nodes[n->a].value) ||
         (n->op == OP_OR && c->nodes[n->a].value))) {
      n->kind = N_CONST;
      n->value = c->nodes[n->a].value;
      n->type = a;
      n->a = n->b = 0;
      break;
    }
    b = expr(c, n->b, expected);
    fold_expression(c, i);
    if (n->kind == N_BYTES) {
      n->type = TY_BYTES;
      break;
    }
    if (n->kind == N_CONST) {
      if (!n->type)
        n->type = infer(n->value);
      break;
    }
    if ((a == TY_BYTES || b == TY_BYTES) && (n->op == '+' || n->op == '*')) {
      if (n->op == '+' ? a != TY_BYTES || b != TY_BYTES
                       : !((a == TY_BYTES && numeric(b)) ||
                           (b == TY_BYTES && numeric(a))))
        fail(c, n->token,
             "bytes concatenation requires bytes operands; repetition requires "
             "an integer count");
      n->type = TY_BYTES;
      n->c = n->op == '*' && type_signed(a == TY_BYTES ? b : a);
      break;
    }
    if ((a == TY_BYTES || b == TY_BYTES) && a != b &&
        (n->op == T_EQ || n->op == T_NE)) {
      n->type = TY_BOOL;
      n->d = 2; /* Different value domains; retain both operand evaluations. */
      break;
    }
    if ((a == TY_STR && b == TY_STR) || (a == TY_BYTES && b == TY_BYTES)) {
      if (n->op == T_EQ || n->op == T_NE || n->op == '<' || n->op == '>' ||
          n->op == T_LE || n->op == T_GE)
        n->type = TY_BOOL;
      else if (n->op == OP_AND || n->op == OP_OR)
        n->type = a;
      else
        fail(c, n->token, "unsupported string operator");
      break;
    }
    if (!number(a) || !number(b))
      fail(c, n->token, "binary operator requires integers");
    if (a == TY_FLOAT || b == TY_FLOAT || n->op == '/' ||
        (n->op == T_POWER && power_type(c, a, b, n->b) == TY_FLOAT)) {
      if (n->op == OP_AND || n->op == OP_OR) {
        if (a != b)
          fail(c, n->token, "float boolean operands require matching types");
        n->type = TY_FLOAT;
        break;
      }
      if (n->op != '+' && n->op != '-' && n->op != '*' && n->op != '/' &&
          n->op != T_FLOORDIV && n->op != '%' && n->op != T_POWER &&
          n->op != T_EQ && n->op != T_NE && n->op != '<' && n->op != '>' &&
          n->op != T_LE && n->op != T_GE)
        fail(c, n->token, "unsupported float operator");
      assignable(c, n->a, TY_FLOAT);
      assignable(c, n->b, TY_FLOAT);
      n->type = n->op == '+' || n->op == '-' || n->op == '*' || n->op == '/' ||
                        n->op == T_POWER || n->op == T_FLOORDIV || n->op == '%'
                    ? TY_FLOAT
                    : TY_BOOL;
      break;
    }
    if ((n->op == OP_AND || n->op == OP_OR) && enum_type(a) && a == b) {
      n->type = a;
      break;
    }
    if ((n->op == OP_AND || n->op == OP_OR) && (enum_type(a) || enum_type(b)))
      fail(c, n->token, "IntEnum boolean operands require matching types");
    a = integer_type(a);
    b = integer_type(b);
    expected = integer_type(expected);
    if (n->op == T_POWER) {
      n->type = a == TY_BOOL ? TY_U8 : a;
      if (numeric(expected) && expected != TY_BOOL &&
          type_size(expected) >= type_size(n->type) &&
          (!type_signed(n->type) || type_signed(expected))) {
        assignable(c, n->a, expected);
        n->type = expected;
      }
      break;
    }
    if (numeric(expected) && expected != TY_BOOL &&
        (c->nodes[n->a].kind == N_CONST || c->nodes[n->b].kind == N_CONST)) {
      if (c->nodes[n->a].kind == N_CONST &&
          fits(c->nodes[n->a].value, expected))
        a = c->nodes[n->a].type = expected;
      if (c->nodes[n->b].kind == N_CONST &&
          fits(c->nodes[n->b].value, expected))
        b = c->nodes[n->b].type = expected;
    }
    if ((a == TY_U16 && type_signed(b)) || (b == TY_U16 && type_signed(a)))
      fail(c, n->token,
           "mixing u16 and signed values requires an explicit cast");
    n->type = type_size(a) > type_size(b) ? a : b;
    if (type_signed(a) != type_signed(b))
      n->type = TY_I16;
    if (a == b)
      n->type = a;
    if (n->type == TY_BOOL)
      n->type = TY_U8;
    if (n->op == T_EQ || n->op == T_NE || n->op == '<' || n->op == '>' ||
        n->op == T_LE || n->op == T_GE) {
      n->d = type_signed(a) || type_signed(b);
      n->type = TY_BOOL;
    }
    if ((n->op == OP_AND || n->op == OP_OR) && a == TY_BOOL && b == TY_BOOL)
      n->type = TY_BOOL;
    if (n->op == T_SHL || n->op == T_SHR)
      n->type = a == TY_BOOL ? TY_U8 : a;
    break;
  case N_BYTES_SEARCH: {
    int count = 0, args[3] = {0}, flags = 6, literals = 1, k;
    a = expr(c, n->a, 0);
    if (a != TY_BYTES)
      fail(c, n->token, "bytes methods require a bytes receiver");
    for (j = n->b; j; j = c->nodes[j].next) {
      if (count == 3)
        fail(c, n->token,
             "bytes search methods expect a needle and optional start/end");
      args[count++] = j;
    }
    if (!count)
      fail(c, n->token,
           "bytes search methods expect a needle and optional start/end");
    b = expr(c, args[0], 0);
    if (b != TY_BYTES && !(numeric(b) && n->op < BS_PREFIX))
      fail(c, n->token,
           "bytes search requires bytes or an integer byte; "
           "startswith/endswith require bytes");
    if (b != TY_BYTES) {
      flags |= 1;
      if (c->nodes[args[0]].kind == N_CONST &&
          (c->nodes[args[0]].value < 0 || c->nodes[args[0]].value > 255))
        fail(c, n->token, "integer byte needles must be in 0..255");
    }
    if (c->nodes[args[0]].kind != N_BYTES && c->nodes[args[0]].kind != N_CONST)
      literals = 0;
    for (k = 1; k < count; ++k) {
      Node *bound = &c->nodes[args[k]];
      if (bound->kind == N_NONE) {
        continue;
      }
      b = expr(c, args[k], 0);
      if (!numeric(b))
        fail(c, bound->token,
             "bytes search bounds require integers or literal None");
      flags &= ~(1 << k);
      if (type_signed(b))
        flags |= 4 << k;
      if (bound->kind != N_CONST)
        literals = 0;
    }
    n->c = flags;
    n->type = bytes_method_type(n->op);
    if (literals && c->nodes[n->a].kind == N_BYTES)
      bytes_search_literal(c, i);
    break;
  }
  case N_CALL: {
    if (c->nodes[n->a].kind == N_ATTR &&
        !module_namespace(c, c->nodes[n->a].a)) {
      int base = c->nodes[n->a].a;
      int class_base = 0;
      if (named_expression(c, base)) {
        char base_name[192];
        int base_module;
        resolve_name(c, base, &base_module, base_name, sizeof(base_name));
        class_base = class_named(c, base_module, base_name);
      }
      if (!class_base && expr(c, base, 0) == TY_BYTES) {
        char method[64];
        token_text(c, c->nodes[n->a].token, method, sizeof(method));
        n->op = bytes_method(method);
        if (!n->op)
          fail(c, n->token, "unsupported bytes method '%s'", method);
        n->kind = N_BYTES_SEARCH;
        n->a = base;
        --c->depth;
        return expr(c, i, expected);
      }
    }
    char name[192];
    int bi = 0, count = 0, args[16], f = -1, module, runtime_len = 0;
    resolve_name(c, n->a, &module, name, sizeof(name));
    if (asset_kind(name)) {
      for (j = n->b; j; j = c->nodes[j].next)
        expr(c, j, TY_STR);
      asset_load(c, i, asset_kind(name));
      --c->depth;
      return TY_BYTES;
    }
    int constructor = class_named(c, module, name), receiver = 0;
    if (constructor) {
      if (!n->c)
        fail(c, n->token, "assign the constructor to a named object variable");
      snprintf(name, sizeof(name), "%s.__init__",
               c->classes[constructor - TY_CLASS_BASE].name);
      n->kind = N_CONSTRUCT;
      receiver = node_new(c, N_NAME, n->token);
      /* c stores a symbol index + 1; a pre-resolved receiver is synthesized
       * below. */
      c->nodes[receiver].token = n->token;
      c->nodes[receiver].symbol = n->c - 1;
      c->nodes[receiver].type = constructor;
    } else if (c->nodes[n->a].kind == N_ATTR && n->symbol >= 0) {
      Function *method = &c->functions[n->symbol];
      if (object_type(c->nodes[method->node].c)) {
        int base = c->nodes[n->a].a;
        /* Unbound Class.method(...) already supplies its receiver explicitly.
         */
        char base_name[192];
        int base_module;
        resolve_name(c, base, &base_module, base_name, sizeof(base_name));
        if (!class_named(c, base_module, base_name) && !n->d) {
          receiver = base;
          expr(c, receiver, 0);
        }
        strcpy(name, method->name);
        module = method->module;
      }
    }
    if (receiver) {
      c->nodes[receiver].next = n->b;
      n->b = receiver;
      n->d = 1;
    }
    if (!strcmp(name, "print"))
      bi = BI_PRINT;
    else if (!strcmp(name, "input"))
      bi = BI_INPUT;
    else if (!strcmp(name, "int"))
      bi = BI_INT;
    else if (!strcmp(name, "pow"))
      bi = BI_POW;
    else if (!strncmp(name, "pyc64._math.", 12)) {
      static const char *exports[] = {"pow",  "sqrt",  "exp",  "log",  "sin",
                                      "cos",  "tan",   "atan", "fabs", "floor",
                                      "ceil", "trunc", NULL};
      for (j = 0; exports[j] && strcmp(exports[j], name + 12); ++j)
        ;
      if (!exports[j])
        fail(c, n->token, "unsupported math function '%s'", name);
      bi = BI_MATH_POW + j;
    } else if (!strcmp(name, "c64.video.screen.clear"))
      bi = BI_CLEAR;
    else if (!strcmp(name, "c64.video.screen.put_char"))
      bi = BI_PUTCHAR;
    else if (!strcmp(name, "c64.assets.address"))
      bi = BI_ASSET_ADDRESS;
    else if (!strcmp(name, "c64._video.mode"))
      bi = BI_VIDEO_MODE;
    else if (!strcmp(name, "c64._music.start"))
      bi = BI_MUSIC_START;
    else if (!strcmp(name, "c64._music.stop"))
      bi = BI_MUSIC_STOP;
    else if (!strcmp(name, "c64._music.active"))
      bi = BI_MUSIC_ACTIVE;
    else if (!strcmp(name, "c64._music.ticks"))
      bi = BI_MUSIC_TICKS;
    else if (!strcmp(name, "c64.memory.peek"))
      bi = BI_PEEK;
    else if (!strcmp(name, "c64.memory.poke"))
      bi = BI_POKE;
    else if (!strcmp(name, "c64._reu.present"))
      bi = BI_REU_PRESENT;
    else if (!strcmp(name, "c64._reu.transfer"))
      bi = BI_REU_TRANSFER;
    else if (!strcmp(name, "c64.irq.on_raster"))
      bi = BI_IRQ_INSTALL;
    else if (!strcmp(name, "c64.irq.set_line"))
      bi = BI_IRQ_LINE;
    else if (!strcmp(name, "c64.irq.off"))
      bi = BI_IRQ_OFF;
    else if (!strcmp(name, "c64.irq.active"))
      bi = BI_IRQ_ACTIVE;
    else if (!strcmp(name, "c64.irq.disable"))
      bi = BI_IRQ_DISABLE;
    else if (!strcmp(name, "c64.irq.restore"))
      bi = BI_IRQ_RESTORE;
    else if (!strcmp(name, "len"))
      bi = BI_LEN;
    else if (!strcmp(name, "bytes"))
      bi = BI_BYTES;
    else if (!strcmp(name, "ord"))
      bi = BI_ORD;
    else if (!strcmp(name, "chr"))
      bi = BI_CHR;
    else if (!strcmp(name, "pyc64.types.array"))
      bi = BI_ARRAY;
    else if (!strcmp(name, "bool"))
      bi = BI_CAST_BASE + TY_BOOL;
    else if (!strcmp(name, "c64.keyboard.read"))
      bi = BI_KEY_READ;
    else if (!strcmp(name, "c64.keyboard.clear"))
      bi = BI_KEY_CLEAR;
    else if (!strcmp(name, "c64.sprite.enable"))
      bi = BI_SPRITE_ENABLE;
    else if (!strcmp(name, "c64.sprite.disable"))
      bi = BI_SPRITE_DISABLE;
    else if (!strcmp(name, "c64.sprite.position"))
      bi = BI_SPRITE_POSITION;
    else if (!strcmp(name, "c64.sprite.color"))
      bi = BI_SPRITE_COLOR;
    else if (!strcmp(name, "c64.sprite.data"))
      bi = BI_SPRITE_DATA;
    else if (!strcmp(name, "c64.kernal.open"))
      bi = BI_FILE_OPEN;
    else if (!strcmp(name, "c64.kernal.close"))
      bi = BI_FILE_CLOSE;
    else if (!strcmp(name, "c64.kernal.read_byte"))
      bi = BI_FILE_READ;
    else if (!strcmp(name, "c64.kernal.write_byte"))
      bi = BI_FILE_WRITE;
    else if (!strcmp(name, "c64.kernal.write_text"))
      bi = BI_FILE_TEXT;
    else if (!strcmp(name, "c64.kernal.status"))
      bi = BI_FILE_STATUS;
    else if (!strcmp(name, "c64.kernal.error"))
      bi = BI_FILE_ERROR;
    else {
      j = library_type(c, n->a);
      if ((j >= TY_U8 && j <= TY_I16) || j == TY_FLOAT)
        bi = BI_CAST_BASE + j;
    }
    for (j = n->b; j; j = c->nodes[j].next) {
      if (count == 16)
        fail(c, n->token, "at most 16 arguments are supported");
      args[count++] = j;
    }
    if (bi == BI_POW) {
      if (count != 2)
        fail(c, n->token, "pow expects two arguments");
      n->kind = N_BINARY;
      n->op = T_POWER;
      n->a = args[0];
      n->b = args[1];
      c->nodes[args[0]].next = 0;
      c->nodes[args[1]].next = 0;
      n->symbol = -1;
      expr(c, i, expected);
      break;
    }
    if (!bi) {
      for (j = 0; j < c->nfunctions; ++j)
        if (module == c->functions[j].module &&
            !strcmp(name, c->functions[j].name))
          f = j;
      if (f < 0) {
        if (constructor && count == 1) {
          n->symbol = -1;
          n->type = constructor;
          break;
        }
        fail(c, n->token, "unsupported or unknown function '%s'", name);
      }
      if (count != c->functions[f].argc)
        fail(c, n->token, "function '%s' expects %d arguments", name,
             c->functions[f].argc);
      for (j = 0; j < count; ++j) {
        Symbol *parameter = &c->symbols[c->functions[f].param[j]];
        if (object_type(parameter->type)) {
          int src = expr(c, args[j], 0);
          Node *arg = &c->nodes[args[j]];
          if (src != parameter->type)
            fail(c, arg->token, "object argument requires the same class type");
          if (arg->kind != N_NAME)
            fail(c, arg->token, "object arguments must be named variables");
          if (!constructor || j)
            object_ready(c, arg->symbol, arg->token);
        } else if (parameter->reference) {
          Node *arg = &c->nodes[args[j]];
          int src = expr(c, args[j], 0);
          Symbol *actual;
          if (src != parameter->type)
            fail(c, arg->token, "container argument requires %s, got %s",
                 type_name(parameter->type), type_name(src));
          if (arg->kind != N_NAME || arg->symbol < 0)
            fail(c, arg->token,
                 "assign the container to a variable before passing it to a "
                 "function");
          actual = &c->symbols[arg->symbol];
          if (actual->element != parameter->element &&
              !(actual->type == TY_LIST && !actual->count && !actual->element))
            fail(c, arg->token,
                 "container parameter element types must match exactly (%s "
                 "versus %s)",
                 type_name(parameter->element), type_name(actual->element));
          parameter_extent(c, parameter);
          if (parameter->count >= 0 && actual->count >= 0 &&
              parameter->count != actual->count)
            fail(c, arg->token, "array parameter expects %d elements, got %d",
                 parameter->count, actual->count);
        } else
          assignable(c, args[j], parameter->type);
      }
      n->symbol = f;
      n->type = constructor ? constructor : c->functions[f].type;
    } else {
      int want = bi == BI_CLEAR || bi == BI_KEY_READ || bi == BI_KEY_CLEAR ||
                         bi == BI_FILE_STATUS || bi == BI_FILE_ERROR ||
                         bi == BI_REU_PRESENT || bi == BI_IRQ_OFF ||
                         bi == BI_IRQ_ACTIVE || bi == BI_IRQ_DISABLE ||
                         bi == BI_MUSIC_STOP || bi == BI_MUSIC_ACTIVE ||
                         bi == BI_MUSIC_TICKS
                     ? 0
                 : bi == BI_REU_TRANSFER                        ? 6
                 : bi == BI_FILE_OPEN                           ? 4
                 : bi == BI_PUTCHAR || bi == BI_SPRITE_POSITION ? 3
                 : bi == BI_MUSIC_START || bi == BI_IRQ_INSTALL ||
                         bi == BI_POKE || bi == BI_ARRAY || bi == BI_MATH_POW ||
                         bi == BI_SPRITE_COLOR || bi == BI_SPRITE_DATA ||
                         bi == BI_FILE_WRITE || bi == BI_FILE_TEXT
                     ? 2
                     : 1;
      if (bi == BI_INPUT && count > 1)
        fail(c, n->token, "input expects zero or one string prompt");
      if (bi == BI_INT) {
        if (count > 1)
          fail(c, n->token,
               "int expects zero or one argument; bases are unsupported");
        want = count;
      }
      if (bi == BI_BYTES) {
        if (count > 2)
          fail(c, n->token,
               "bytes expects zero, one value or text plus encoding");
        want = count;
      }
      if (bi == BI_CAST_BASE + TY_FLOAT && count == 0)
        want = 0;
      if (bi != BI_PRINT && bi != BI_INPUT && count != want)
        fail(c, n->token, "'%s' expects %d arguments", name, want);
      n->symbol = -bi;
      n->type = TY_VOID;
      if (bi >= BI_MATH_POW && bi <= BI_MATH_TRUNC) {
        for (j = 0; j < count; ++j)
          assignable(c, args[j], TY_FLOAT);
        n->type = bi >= BI_MATH_FLOOR ? TY_I16 : TY_FLOAT;
      } else if (bi == BI_BYTES) {
        unsigned char data[MAX_BYTES];
        Node *value = count ? &c->nodes[args[0]] : NULL;
        int length = 0, literal = 1;
        n->type = TY_BYTES;
        if (!count) {
          bytes_literal(c, i, data, 0);
          break;
        }
        if (value->kind == N_LIST) {
          if (count != 1)
            fail(c, n->token, "bytes encoding requires text");
          value->type = TY_LIST;
          value->c = TY_U16; /* Preserve each integral value's full word. */
          for (j = value->a; j; j = c->nodes[j].next) {
            a = expr(c, j, 0);
            if (!numeric(a))
              fail(c, c->nodes[j].token, "bytes elements require integers");
            if (c->nodes[j].kind == N_CONST) {
              if (c->nodes[j].value < 0 || c->nodes[j].value > 255)
                fail(c, c->nodes[j].token, "bytes elements must fit 0..255");
              if (length == MAX_BYTES)
                fail(c, n->token, "bytes literal arena exhausted");
              data[length] = (unsigned char)c->nodes[j].value;
            } else
              literal = 0;
            ++length;
          }
          n->op = 2;
          n->c = TY_U16;
          if (literal)
            bytes_literal(c, i, data, length);
          break;
        }
        a = expr(c, args[0], 0);
        if (count == 2) {
          char encoding[32];
          int k;
          if (a != TY_STR || expr(c, args[1], 0) != TY_STR ||
              c->nodes[args[1]].kind != N_STRING)
            fail(c, n->token,
                 "bytes(text, encoding) requires text and a constant encoding");
          if (c->nodes[args[1]].d >= (int)sizeof(encoding))
            fail(c, n->token, "unsupported bytes encoding");
          strcpy(encoding, c->strings + c->nodes[args[1]].value);
          for (k = 0; encoding[k]; ++k) {
            if (encoding[k] >= 'A' && encoding[k] <= 'Z')
              encoding[k] += 'a' - 'A';
            if (encoding[k] == '_')
              encoding[k] = '-';
          }
          if (strcmp(encoding, "ascii") && strcmp(encoding, "utf-8") &&
              strcmp(encoding, "utf8"))
            fail(c, n->token,
                 "bytes encoding supports ASCII and UTF-8 for the ASCII string "
                 "subset");
          n->op = 3;
          if (value->kind == N_STRING)
            bytes_literal(c, i, (unsigned char *)c->strings + value->value,
                          value->d);
        } else if (a == TY_BYTES) {
          n->op = 0;
          if (value->kind == N_BYTES)
            bytes_literal(c, i, c->bytes + value->value, value->d);
        } else if (numeric(a)) {
          n->op = 1;
          if (value->kind == N_CONST) {
            length = (int)value->value;
            if (length < 0 || length > MAX_BYTES - 2)
              fail(c, n->token, "bytes count must fit the literal arena");
            memset(data, 0, (size_t)length);
            bytes_literal(c, i, data, length);
          }
        } else if (a == TY_ARRAY || a == TY_LIST) {
          if (value->kind != N_NAME)
            fail(c, n->token,
                 "bytes requires a named array/list or list literal");
          n->c = c->symbols[value->symbol].element;
          if (!numeric(n->c))
            fail(c, n->token, "bytes elements require integers");
          n->op = 2;
        } else
          fail(c, n->token,
               "bytes requires a count, bytes or an integer sequence; text "
               "requires an encoding");
      } else if (bi == BI_INT) {
        a = count ? expr(c, args[0], 0) : TY_I16;
        if (!number(a) && a != TY_STR)
          fail(c, n->token,
               "int requires an integer, bool, float or decimal string");
        n->type = a == TY_FLOAT || a == TY_STR ? TY_I16
                  : a == TY_BOOL               ? TY_U8
                                               : integer_type(a);
        if (count && a == TY_FLOAT && c->nodes[args[0]].kind == N_FLOAT)
          float_integer(c, n->token, c->nodes[args[0]].real, 1);
        if (!count ||
            (c->constant_context && (c->nodes[args[0]].kind == N_CONST ||
                                     c->nodes[args[0]].kind == N_FLOAT))) {
          long value = !count ? 0 : c->nodes[args[0]].value;
          if (a == TY_FLOAT) {
            value = float_integer(c, n->token, c->nodes[args[0]].real, 1);
            if (value >= 32768)
              value -= 65536;
          }
          n->kind = N_CONST;
          n->value = value;
          n->a = n->b = 0;
        }
      } else if (bi == BI_ARRAY) {
        n->c = library_type(c, args[0]);
        ++c->constant_context;
        expr(c, args[1], 0);
        --c->constant_context;
        if (c->nodes[args[1]].kind != N_CONST)
          fail(c, n->token, "array size must be a compile-time integer");
        n->d = (int)c->nodes[args[1]].value;
        if (!number(n->c) || n->d <= 0 || n->d > 16384)
          fail(c, n->token, "array must have 1..16384 integer elements");
        if (enum_type(n->c) && enum_value_member(c, n->c, 0) < 0)
          fail(c, n->token, "zero-filled IntEnum arrays require a zero member");
        n->type = TY_ARRAY;
      } else if (bi == BI_ORD) {
        a = expr(c, args[0], 0);
        if (a != TY_STR && a != TY_BYTES)
          fail(c, n->token,
               "ord requires a one-character string or bytes value");
        n->type = TY_U8;
        if (c->nodes[args[0]].kind == N_STRING ||
            c->nodes[args[0]].kind == N_BYTES) {
          if (c->nodes[args[0]].d != 1)
            fail(c, n->token, "ord requires a one-character string");
          n->value = a == TY_BYTES
                         ? c->bytes[c->nodes[args[0]].value]
                         : (unsigned char)c->strings[c->nodes[args[0]].value];
          n->kind = N_CONST;
          n->a = n->b = 0;
        }
      } else if (bi == BI_CHR) {
        a = expr(c, args[0], 0);
        if (!numeric(a))
          fail(c, n->token, "chr requires an integer");
        n->type = TY_STR;
        if (c->nodes[args[0]].kind == N_CONST &&
            (c->nodes[args[0]].value < 1 || c->nodes[args[0]].value > 127))
          fail(c, n->token, "chr supports ASCII values in 1..127");
        if (c->nodes[args[0]].kind == N_CONST)
          constant_character(c, n, (unsigned char)c->nodes[args[0]].value);
      } else if (bi == BI_INPUT) {
        if (count)
          assignable(c, args[0], TY_STR);
        n->type = TY_STR;
      } else if (bi == BI_PRINT) {
        for (j = 0; j < count; ++j) {
          a = expr(c, args[j], 0);
          if (a != TY_STR && a != TY_BYTES && !number(a))
            fail(c, c->nodes[args[j]].token,
                 "print supports strings, integers and booleans");
        }
      } else if (bi == BI_ASSET_ADDRESS) {
        assignable(c, args[0], TY_BYTES);
        n->type = TY_U16;
      } else if (bi == BI_VIDEO_MODE) {
        assignable(c, args[0], TY_U8);
        if (c->nodes[args[0]].kind == N_CONST && c->nodes[args[0]].value > 1)
          fail(c, n->token, "video mode must be 0..1");
      } else if (bi == BI_KEY_READ) {
        n->type = TY_U8;
      } else if (bi >= BI_IRQ_INSTALL && bi <= BI_IRQ_RESTORE) {
        if (bi == BI_IRQ_INSTALL || bi == BI_IRQ_LINE) {
          assignable(c, args[0], TY_U16);
          if (c->nodes[args[0]].kind == N_CONST &&
              c->nodes[args[0]].value > 511)
            fail(c, n->token, "raster line must be in 0..511");
        }
        if (bi == BI_IRQ_INSTALL) {
          int callback = irq_handler(c, args[1]);
          for (f = 0; f < c->nfunctions; ++f)
            if (c->functions[f].node == callback)
              break;
          if (f == c->nfunctions)
            fail(c, n->token, "internal error: missing IRQ callback");
          n->value = f + 1;
          c->nodes[args[1]].type = TY_VOID;
        }
        if (bi == BI_IRQ_RESTORE) {
          assignable(c, args[0], TY_BOOL);
        } else
          n->type = TY_BOOL;
      } else if (bi >= BI_MUSIC_START && bi <= BI_MUSIC_TICKS) {
        if (bi == BI_MUSIC_START) {
          assignable(c, args[0], TY_BYTES);
          assignable(c, args[1], TY_U16);
        }
        n->type = bi == BI_MUSIC_TICKS ? TY_U16 : TY_BOOL;
      } else if (bi == BI_REU_PRESENT || bi == BI_REU_TRANSFER) {
        for (j = 0; j < count; ++j)
          assignable(c, args[j], j == 0 || j == 1 || j == 3 ? TY_U16 : TY_U8);
        n->type = bi == BI_REU_PRESENT ? TY_BOOL : TY_U8;
      } else if (bi >= BI_FILE_OPEN && bi <= BI_FILE_ERROR) {
        for (j = 0; j < count; ++j)
          assignable(c, args[j],
                     j == 3 || (bi == BI_FILE_TEXT && j == 1) ? TY_STR : TY_U8);
        if (bi == BI_FILE_OPEN) {
          if (c->nodes[args[3]].kind == N_STRING && c->nodes[args[3]].d > 255)
            fail(c, n->token, "KERNAL filename must have at most 255 bytes");
          n->type = TY_BOOL;
        } else if (bi == BI_FILE_WRITE || bi == BI_FILE_TEXT)
          n->type = TY_BOOL;
        else if (bi != BI_FILE_CLOSE)
          n->type = TY_U8;
      } else if (bi >= BI_SPRITE_ENABLE && bi <= BI_SPRITE_DATA) {
        assignable(c, args[0], TY_U8);
        if (c->nodes[args[0]].kind == N_CONST && c->nodes[args[0]].value > 7)
          fail(c, n->token, "sprite index must be 0..7");
        if (bi == BI_SPRITE_POSITION || bi == BI_SPRITE_DATA)
          assignable(c, args[1], TY_U16);
        if (bi == BI_SPRITE_POSITION) {
          assignable(c, args[2], TY_U8);
          if (c->nodes[args[1]].kind == N_CONST &&
              c->nodes[args[1]].value > 511)
            fail(c, n->token, "sprite X coordinate must be 0..511");
        } else if (bi == BI_SPRITE_COLOR) {
          assignable(c, args[1], TY_U8);
          if (c->nodes[args[1]].kind == N_CONST && c->nodes[args[1]].value > 15)
            fail(c, n->token, "sprite color must be 0..15");
        } else if (bi == BI_SPRITE_DATA && c->nodes[args[1]].kind == N_CONST) {
          long address = c->nodes[args[1]].value;
          if (address >= 16384 || address % 64)
            fail(c, n->token,
                 "sprite data must be 64-byte aligned in VIC bank 0");
        }
      } else if (bi == BI_PUTCHAR) {
        assignable(c, args[0], TY_U8);
        assignable(c, args[1], TY_U8);
        a = expr(c, args[2], 0);
        if (a == TY_STR) {
          if (c->nodes[args[2]].kind != N_STRING || c->nodes[args[2]].d != 1)
            fail(c, n->token,
                 "put_char requires a one-character literal or a screen-code "
                 "integer");
        } else
          assignable(c, args[2], TY_U8);
      } else if (bi == BI_PEEK || bi == BI_POKE) {
        assignable(c, args[0], TY_U16);
        if (bi == BI_POKE)
          assignable(c, args[1], TY_U8);
        else
          n->type = TY_U8;
      } else if (bi == BI_LEN) {
        a = expr(c, args[0], 0);
        if (a == TY_ARRAY || a == TY_LIST) {
          if (c->nodes[args[0]].symbol < 0)
            fail(c, n->token,
                 "assign the list literal to a variable before using len");
          n->value = c->symbols[c->nodes[args[0]].symbol].count;
          runtime_len = c->symbols[c->nodes[args[0]].symbol].reference;
        } else if (a == TY_STR || a == TY_BYTES) {
          runtime_len = c->nodes[args[0]].kind != N_STRING &&
                        c->nodes[args[0]].kind != N_BYTES;
          if (!runtime_len)
            n->value = c->nodes[args[0]].d;
        } else
          fail(c, n->token, "len requires an array/list variable or a string");
        n->type = TY_U16;
      } else if (bi == BI_CAST_BASE + TY_FLOAT) {
        if (!count) {
          n->kind = N_FLOAT;
          n->real = 0;
          n->a = n->b = 0;
        } else {
          a = expr(c, args[0], 0);
          if (!number(a) && a != TY_STR)
            fail(c, n->token,
                 "float requires an integer, bool, float or string");
          if (c->nodes[args[0]].kind == N_CONST ||
              c->nodes[args[0]].kind == N_FLOAT) {
            n->real = a == TY_FLOAT ? c->nodes[args[0]].real
                                    : (double)c->nodes[args[0]].value;
            n->kind = N_FLOAT;
            n->a = n->b = 0;
          }
        }
        n->type = TY_FLOAT;
      } else if (bi > BI_CAST_BASE) {
        a = expr(c, args[0], 0);
        if (!number(a) &&
            !(bi == BI_CAST_BASE + TY_BOOL && (a == TY_STR || a == TY_BYTES)))
          fail(c, n->token, "integer cast requires an integer");
        n->type = bi - BI_CAST_BASE;
        if (c->constant_context && a == TY_FLOAT && n->type != TY_BOOL &&
            c->nodes[args[0]].kind == N_FLOAT) {
          long value = float_integer(c, n->token, c->nodes[args[0]].real, 0);
          long mask = type_size(n->type) == 1 ? 255 : 65535;
          value &= mask;
          if (type_signed(n->type) && value > mask / 2)
            value -= mask + 1;
          n->value = value;
          n->kind = N_CONST;
          n->a = n->b = 0;
        }
        if (c->constant_context && n->type == TY_BOOL &&
            (c->nodes[args[0]].kind == N_STRING ||
             c->nodes[args[0]].kind == N_BYTES)) {
          n->value = c->nodes[args[0]].d != 0;
          n->kind = N_CONST;
          n->a = n->b = 0;
        }
        if (c->constant_context && c->nodes[args[0]].kind == N_CONST) {
          long value = c->nodes[args[0]].value;
          long mask = type_size(n->type) == 1 ? 255 : 65535;
          value = n->type == TY_BOOL ? value != 0 : value & mask;
          if (type_signed(n->type) && value > mask / 2)
            value -= mask + 1;
          n->value = value;
          n->kind = N_CONST;
          n->a = n->b = 0;
        }
      }
      if (bi == BI_LEN && !runtime_len) {
        n->kind = N_CONST;
        n->a = n->b = 0;
      }
    }
    break;
  }
  default:
    fail(c, n->token, "invalid expression");
  }
  --c->depth;
  return n->type;
}
/* Breaks in a nested loop body belong to that nested loop. Its else suite,
   however, executes outside that loop and may break this loop. */
static int breaks_loop(Compiler *c, int i) {
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    if (n->kind == N_BREAK)
      return 1;
    if (n->kind == N_IF && (breaks_loop(c, n->b) || breaks_loop(c, n->c)))
      return 1;
    if (breaks_loop(c, n->else_body))
      return 1;
  }
  return 0;
}
static int returns(Compiler *c, int i) {
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    if (n->kind == N_RETURN)
      return 1;
    if (n->kind == N_IF && n->c && returns(c, n->b) && returns(c, n->c))
      return 1;
    if ((n->kind == N_WHILE || n->kind == N_FOR || n->kind == N_EACH) &&
        n->else_body &&
        !breaks_loop(c, n->kind == N_WHILE ? n->b : (int)n->value) &&
        returns(c, n->else_body))
      return 1;
  }
  return 0;
}
static void block(Compiler *c, int i) {
  if (++c->depth > MAX_DEPTH)
    fail(c, c->nodes[i].token, "block nesting is too deep");
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    int type = n->type, j;
    c->active_module = c->tokens[n->token].module;
    switch (n->kind) {
    case N_ENUMDEF:
      break;
    case N_CLASS: {
      ClassLayout *layout = &c->classes[n->type - TY_CLASS_BASE];
      for (j = 0; j < layout->nfields; ++j) {
        Node *field = &c->nodes[layout->fields[j]];
        if (!field->b)
          continue;
        ++c->constant_context;
        assignable(c, field->b, field->type);
        --c->constant_context;
        if (c->nodes[field->b].kind != N_CONST &&
            c->nodes[field->b].kind != N_STRING &&
            c->nodes[field->b].kind != N_BYTES &&
            c->nodes[field->b].kind != N_FLOAT)
          fail(c, field->token,
               "field defaults must be compile-time scalar values");
      }
      break;
    }
    case N_DECLCONST: {
      Node *value;
      char name[64];
      token_text(c, c->nodes[n->a].token, name, sizeof(name));
      if (find_symbol(c, name, 0, c->active_module) >= 0)
        fail(c, n->token, "duplicate declaration '%s'", name);
      ++c->constant_context;
      if (type)
        assignable(c, n->b, type);
      else
        type = expr(c, n->b, 0);
      --c->constant_context;
      value = &c->nodes[n->b];
      if (value->kind != N_CONST && value->kind != N_STRING &&
          value->kind != N_BYTES && value->kind != N_FLOAT)
        fail(c, n->token, "constant initializer must be a compile-time value");
      j = add_symbol(c, c->nodes[n->a].token, type, 0, value->d);
      c->symbols[j].constant = 1;
      c->symbols[j].value = value->value;
      c->symbols[j].real = value->real;
      n->symbol = j;
      n->type = type;
      break;
    }
    case N_ASSIGN: {
      Node *lhs = &c->nodes[n->a];
      if (lhs->kind == N_ATTR) {
        int parent = field_receiver(c, n->a);
        if (parent >= 0) {
          int property =
              class_property(c, c->symbols[parent].type, lhs->token, 1);
          if (property < 0)
            fail(c, lhs->token, "property is read-only");
          if (property) {
            int setter = accessor_function(c, property), call;
            Function *fn = &c->functions[setter];
            object_ready(c, parent, lhs->token);
            if (!n->b)
              fail(c, n->token, "property assignment requires a value");
            assignable(c, n->b, c->symbols[fn->param[1]].type);
            call = node_new(c, N_PROPERTY_CALL, lhs->token);
            c->nodes[call].symbol = setter;
            c->nodes[call].type = TY_VOID;
            c->nodes[call].b = object_argument(c, parent, lhs->token);
            c->nodes[c->nodes[call].b].next = n->b;
            n->kind = N_EXPR;
            n->a = call;
            n->b = 0;
            break;
          }
        }
      }
      if (lhs->kind == N_NAME) {
        char name[64];
        token_text(c, lhs->token, name, sizeof(name));
        j = find_symbol(c, name, c->scope, c->active_module);
        if (j >= 0 && object_type(c->symbols[j].type))
          fail(c, n->token,
               "object rebinding is unsupported; mutate fields instead");
        if (j >= 0 && c->symbols[j].reference)
          fail(c, n->token,
               "rebinding a container parameter is unsupported; mutate indexed "
               "elements instead");
        if (j >= 0 && c->symbols[j].iterating)
          fail(c, n->token,
               "rebinding an iterated container is unsupported; mutate indexed "
               "elements instead");
        if (j >= 0 && c->symbols[j].constant)
          fail(c, n->token, "cannot assign to constant '%s'", name);
        if (j >= 0 && n->op && !c->symbols[j].initialized)
          fail(c, n->token, "variable '%s' may be read before initialization",
               name);
        if (j < 0) {
          if (n->op)
            fail(c, n->token,
                 "augmented assignment requires an existing variable");
          if (!n->b)
            fail(c, n->token,
                 "annotation alone does not initialize a variable; supply a "
                 "value");
          j = add_symbol(c, lhs->token, type, n->c, 0);
          c->symbols[j].initialized = 0;
          if (!type && c->nodes[n->b].kind == N_CALL) {
            int candidate = library_type(c, c->nodes[n->b].a);
            if (object_type(candidate))
              type = candidate;
          }
          if (object_type(type)) {
            Node *value = &c->nodes[n->b];
            if (value->kind != N_CALL || library_type(c, value->a) != type)
              fail(c, n->token,
                   "object variables require a matching constructor; aliases "
                   "are unsupported");
            c->symbols[j].type = type;
            c->symbols[j].count = c->classes[type - TY_CLASS_BASE].size;
            if (!c->symbols[j].count)
              c->symbols[j].count = 1;
            object_fields(c, j, 0);
            value->c = j + 1;
            expr(c, n->b, 0);
            if (value->symbol >= 0)
              for (int field = 0; field < c->nsymbols; ++field)
                if (c->symbols[field].parent == j + 1)
                  c->symbols[field].initialized = 1;
            c->symbols[j].initialized = 1;
            lhs->symbol = j;
            lhs->type = n->type = type;
            break;
          }
          if (!type) {
            if (!n->b)
              fail(c, n->token, "declaration requires a type or value");
            type = expr(c, n->b, 0);
            if (type == TY_ARRAY || type == TY_LIST) {
              n->c = c->nodes[n->b].c;
              n->d = c->nodes[n->b].d;
            }
          }
          if (object_type(type))
            fail(c, n->token,
                 "object aliases are unsupported; pass named objects to "
                 "functions");
          if (type == TY_ARRAY) {
            if (n->type == TY_ARRAY) {
              ++c->constant_context;
              expr(c, n->d, 0);
              --c->constant_context;
              if (c->nodes[n->d].kind != N_CONST)
                fail(c, n->token, "array size must be a compile-time integer");
              n->d = (int)c->nodes[n->d].value;
            }
            if (n->d <= 0 || n->d > 16384 || !number(n->c))
              fail(c, n->token, "array must have 1..16384 integer elements");
          } else if (type == TY_LIST) {
            if (c->nodes[n->b].kind != N_LIST)
              fail(c, n->token,
                   "list variables require a list literal; list aliases are "
                   "unsupported");
            n->d = c->nodes[n->b].d;
            if (!n->c)
              n->c = c->nodes[n->b].c;
            if (n->c)
              for (int item = c->nodes[n->b].a; item;
                   item = c->nodes[item].next)
                assignable(c, item, n->c);
            c->nodes[n->b].c = n->c;
          }
          if (!n->b)
            fail(c, n->token,
                 "annotation alone does not initialize a variable; supply a "
                 "value");
          if (type == TY_VOID)
            fail(c, n->token, "cannot store a function with no return value");
          c->symbols[j].type = type;
          c->symbols[j].element = n->c;
          c->symbols[j].count = n->d;
        } else if (type && type != c->symbols[j].type)
          fail(c, n->token, "conflicting type declaration for '%s'", name);
        lhs->symbol = j;
        lhs->type = type = c->symbols[j].type;
      } else {
        if (lhs->kind == N_SLICE)
          fail(c, n->token,
               "bytes slice assignment is unsupported; bytes are immutable");
        type = lhs->kind == N_ATTR ? field_expression(c, n->a, 1) : 0;
        if (!type)
          type = expr(c, n->a, 0);
        if (lhs->kind == N_INDEX && (c->nodes[lhs->a].type == TY_STR ||
                                     c->nodes[lhs->a].type == TY_BYTES))
          fail(c, n->token,
               c->nodes[lhs->a].type == TY_BYTES
                   ? "bytes are immutable; assign a new bytes value instead"
                   : "strings are immutable; assign a new string instead");
        if (lhs->symbol >= 0 && c->symbols[lhs->symbol].constant)
          fail(c, n->token, "cannot assign to constant '%s'",
               c->symbols[lhs->symbol].name);
        if (lhs->kind != N_HW && lhs->kind != N_INDEX && lhs->kind != N_FIELD)
          fail(c, n->token, "invalid assignment target");
        if (lhs->kind == N_HW && lhs->d)
          fail(c, n->token, "joystick properties are read-only");
      }
      n->type = type;
      if (type == TY_LIST && n->b) {
        Node *value = &c->nodes[n->b];
        Symbol *s = &c->symbols[lhs->symbol];
        if (value->kind != N_LIST)
          fail(c, n->token,
               "list variables require a list literal; list aliases are "
               "unsupported");
        if (value->d != s->count)
          fail(c, n->token, "list size cannot change (expected %d elements)",
               s->count);
        value->c = s->element;
        for (j = value->a; j; j = c->nodes[j].next)
          assignable(c, j, s->element);
        value->type = TY_LIST;
        c->symbols[lhs->symbol].initialized = 1;
        break;
      }
      if (type == TY_ARRAY && n->b) {
        Node *value = &c->nodes[n->b];
        expr(c, n->b, 0);
        if (value->kind != N_CALL || value->symbol != -BI_ARRAY)
          fail(c, n->token,
               "whole-array assignment is unsupported; use array(T, N)");
        if (value->c != c->symbols[lhs->symbol].element ||
            value->d != c->symbols[lhs->symbol].count)
          fail(c, n->token, "array constructor does not match its annotation");
        c->symbols[lhs->symbol].initialized = 1;
        break;
      }
      if (n->op && lhs->kind == N_INDEX)
        fail(c, n->token,
             "augmented array assignment is unsupported; use an explicit "
             "assignment");
      if (n->b)
        assignable(c, n->b, type);
      if (lhs->kind == N_NAME || lhs->kind == N_FIELD)
        c->symbols[lhs->symbol].initialized = 1;
      break;
    }
    case N_IF:
    case N_WHILE: {
      int entry[MAX_SYMBOLS] = {0}, first[MAX_SYMBOLS] = {0}, k;
      int condition_type = expr(c, n->a, 0);
      if (!number(condition_type) && condition_type != TY_STR &&
          condition_type != TY_BYTES)
        fail(c, n->token, "condition must be an integer, bool or string");
      for (k = 0; k < c->nsymbols; ++k)
        entry[k] = c->symbols[k].initialized;
      if (n->kind == N_WHILE)
        ++c->loop_depth;
      block(c, n->b);
      for (k = 0; k < c->nsymbols; ++k) {
        first[k] = c->symbols[k].initialized;
        c->symbols[k].initialized = entry[k];
      }
      if (n->kind == N_WHILE) {
        --c->loop_depth;
        block(c, n->else_body);
        if (breaks_loop(c, n->b))
          for (k = 0; k < c->nsymbols; ++k)
            c->symbols[k].initialized = entry[k];
      } else if (n->c)
        block(c, n->c);
      if (n->kind == N_IF)
        for (k = 0; k < c->nsymbols; ++k)
          c->symbols[k].initialized &= first[k];
      break;
    }
    case N_FOR: {
      Node *v = &c->nodes[n->a];
      char name[64];
      long start = 0, stop = 0, step = 1, last = 0, count = 0;
      int args[3] = {n->b, n->c, n->d}, k, entry[MAX_SYMBOLS] = {0};
      for (k = 0; k < c->nsymbols; ++k)
        entry[k] = c->symbols[k].initialized;
      for (k = 0; k < 3; ++k)
        if (args[k]) {
          int argument_type;
          ++c->constant_context;
          argument_type = expr(c, args[k], 0);
          --c->constant_context;
          if (!numeric(argument_type))
            fail(c, c->nodes[args[k]].token,
                 "range arguments must be integers");
          if (c->nodes[args[k]].kind != N_CONST)
            n->op = 1; /* Bounds must be captured at runtime. */
          else if (!fits(c->nodes[args[k]].value, TY_I16))
            fail(c, c->nodes[args[k]].token, "range arguments must fit in i16");
        }
      if (n->d && c->nodes[n->d].kind == N_CONST && !c->nodes[n->d].value)
        fail(c, n->token, "range step cannot be zero");
      if (!n->op) {
        stop = c->nodes[n->b].value;
        if (n->c) {
          start = stop;
          stop = c->nodes[n->c].value;
        }
        if (n->d)
          step = c->nodes[n->d].value;
        if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
          count = (step > 0 ? (stop - start - 1) / step
                            : (start - stop - 1) / (-step)) +
                  1;
          last = start + (count - 1) * step;
        }
      }
      token_text(c, v->token, name, sizeof(name));
      j = find_symbol(c, name, c->scope, c->active_module);
      if (j >= 0 && c->symbols[j].constant)
        fail(c, n->token, "cannot use constant '%s' as a loop variable", name);
      if (j < 0)
        j = add_symbol(c, v->token, TY_I16, 0, 0);
      if (!numeric(c->symbols[j].type) || enum_type(c->symbols[j].type))
        fail(c, n->token, "range loop variable must be an integer");
      if (!n->op && count &&
          (!fits(start, c->symbols[j].type) || !fits(last, c->symbols[j].type)))
        fail(c, n->token, "range values do not fit in loop variable type");
      v->symbol = j;
      v->type = c->symbols[j].type;
      n->symbol = j;
      c->symbols[j].initialized = 1;
      ++c->loop_depth;
      block(c, (int)n->value);
      --c->loop_depth;
      for (k = 0; k < c->nsymbols; ++k)
        c->symbols[k].initialized = entry[k];
      block(c, n->else_body);
      if (breaks_loop(c, (int)n->value))
        for (k = 0; k < c->nsymbols; ++k)
          c->symbols[k].initialized = entry[k];
      break;
    }
    case N_EACH: {
      Node *source = &c->nodes[n->b], *v = &c->nodes[n->a];
      char name[64];
      int source_type = expr(c, n->b, 0), element, container = -1;
      int k, entry[MAX_SYMBOLS] = {0};
      if (source_type == TY_STR)
        element = TY_STR;
      else if (source_type == TY_BYTES)
        element = TY_U8;
      else if (source_type == TY_ARRAY || source_type == TY_LIST) {
        if (source->kind == N_NAME) {
          container = source->symbol;
          element = c->symbols[container].element;
        } else if (source->kind == N_LIST ||
                   (source->kind == N_CALL && source->symbol == -BI_ARRAY))
          element = source->c;
        else {
          fail(c, n->token, "iteration requires a named container or literal");
          element = TY_NONE;
        }
        if (!element)
          element = TY_U8; /* An untyped empty list never loads an element. */
      } else {
        fail(c, n->token, "iteration requires a list, array, string or range");
        element = TY_NONE;
      }
      for (k = 0; k < c->nsymbols; ++k)
        entry[k] = c->symbols[k].initialized;
      token_text(c, v->token, name, sizeof(name));
      j = find_symbol(c, name, c->scope, c->active_module);
      if (j >= 0 && c->symbols[j].constant)
        fail(c, n->token, "cannot use constant '%s' as a loop variable", name);
      if (j < 0)
        j = add_symbol(c, v->token, element, 0, 0);
      assign_type(c, v->token, element, c->symbols[j].type);
      n->type = element;
      v->type = c->symbols[j].type;
      v->symbol = n->symbol = j;
      c->symbols[j].initialized = 1;
      if (container >= 0)
        ++c->symbols[container].iterating;
      ++c->loop_depth;
      block(c, (int)n->value);
      --c->loop_depth;
      if (container >= 0)
        --c->symbols[container].iterating;
      for (k = 0; k < c->nsymbols; ++k)
        c->symbols[k].initialized = entry[k];
      block(c, n->else_body);
      if (breaks_loop(c, (int)n->value))
        for (k = 0; k < c->nsymbols; ++k)
          c->symbols[k].initialized = entry[k];
      break;
    }
    case N_ASSERT:
      type = expr(c, n->a, 0);
      if (!number(type) && type != TY_STR && type != TY_BYTES)
        fail(c, n->token,
             "assert condition must be an integer, bool or string");
      if (n->b && expr(c, n->b, 0) != TY_STR)
        fail(c, n->token, "assert message must be a string");
      break;
    case N_RETURN:
      if (!c->scope)
        fail(c, n->token, "return outside a function");
      type = c->functions[c->scope - 1].type;
      if (c->nodes[c->functions[c->scope - 1].node].d)
        object_ready(c, c->functions[c->scope - 1].param[0], n->token);
      if (type == TY_VOID) {
        if (n->a)
          fail(c, n->token, "function has no return type annotation");
      } else {
        if (!n->a)
          fail(c, n->token, "return value required");
        assignable(c, n->a, type);
      }
      break;
    case N_BREAK:
    case N_CONTINUE:
      if (!c->loop_depth)
        fail(c, n->token, "loop control outside a loop");
      break;
    case N_EXPR:
      if (c->nodes[n->a].kind != N_CALL &&
          c->nodes[n->a].kind != N_ENUM_LOOKUP &&
          !((c->nodes[n->a].kind == N_BINARY ||
             c->nodes[n->a].kind == N_CONST) &&
            c->nodes[n->a].c == 1))
        fail(c, n->token,
             "only function calls may be used as expression statements");
      expr(c, n->a, 0);
      if (c->nodes[n->a].type == TY_ARRAY || c->nodes[n->a].type == TY_LIST)
        fail(c, n->token, "array constructor must be assigned to a variable");
      break;
    case N_FUNC:
      if (c->scope || c->depth > 1)
        fail(c, n->token, "functions must be defined at module scope");
      break;
    case N_PASS:
      break;
    default:
      fail(c, n->token, "unsupported statement");
    }
  }
  --c->depth;
}
static void visit_calls(Compiler *c, int i, int depth);
static void visit_function(Compiler *c, int f, int depth) {
  Function *fn = &c->functions[f];
  if (fn->state == 1)
    fail(c, c->nodes[fn->node].token,
         "recursion is unsupported with static function frames");
  if (fn->state == 2)
    return;
  fn->state = 1;
  visit_calls(c, c->nodes[fn->node].b, depth + 1);
  fn->state = 2;
}
static void visit_calls(Compiler *c, int i, int depth) {
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    if (n->kind == N_FUNC || n->kind == N_CLASS || n->kind == N_FIELD ||
        n->kind == N_NAME || n->kind == N_ATTR || n->kind == N_HW ||
        n->kind == N_CONST || n->kind == N_STRING || n->kind == N_PARAM)
      continue;
    if (n->kind == N_CALL || n->kind == N_CONSTRUCT ||
        n->kind == N_PROPERTY_CALL) {
      if (n->symbol >= 0)
        visit_function(c, n->symbol, depth);
      visit_calls(c, n->b, depth);
    } else if (n->kind == N_FOR || n->kind == N_EACH) {
      visit_calls(c, n->b, depth);
      visit_calls(c, n->c, depth);
      visit_calls(c, n->d, depth);
      visit_calls(c, (int)n->value, depth);
      visit_calls(c, n->else_body, depth);
    } else {
      visit_calls(c, n->a, depth);
      visit_calls(c, n->b, depth);
      if (n->kind == N_SLICE) {
        visit_calls(c, n->c, depth);
        visit_calls(c, n->d, depth);
      }
      if (n->kind == N_IF)
        visit_calls(c, n->c, depth);
      if (n->kind == N_WHILE)
        visit_calls(c, n->else_body, depth);
    }
  }
}
static void mark_calls(Compiler *c, int i) {
  for (; i; i = c->nodes[i].next) {
    Node *n = &c->nodes[i];
    if (n->kind == N_FUNC || n->kind == N_CLASS || n->kind == N_FIELD ||
        n->kind == N_CONST || n->kind == N_STRING || n->kind == N_NAME ||
        n->kind == N_HW || n->kind == N_ATTR)
      continue;
    if (n->kind == N_CALL || n->kind == N_CONSTRUCT ||
        n->kind == N_PROPERTY_CALL) {
      if (n->symbol == -BI_IRQ_INSTALL) {
        int callback = (int)n->value - 1;
        c->functions[callback].irq = 1;
        if (!c->functions[callback].used) {
          c->functions[callback].used = 1;
          mark_calls(c, c->nodes[c->functions[callback].node].b);
        }
      }
      if (n->symbol >= 0 && !c->functions[n->symbol].used) {
        c->functions[n->symbol].used = 1;
        mark_calls(c, c->nodes[c->functions[n->symbol].node].b);
      }
      mark_calls(c, n->b);
    } else if (n->kind == N_FOR || n->kind == N_EACH) {
      mark_calls(c, n->b);
      mark_calls(c, n->c);
      mark_calls(c, n->d);
      mark_calls(c, (int)n->value);
      mark_calls(c, n->else_body);
    } else {
      mark_calls(c, n->a);
      mark_calls(c, n->b);
      if (n->kind == N_SLICE) {
        mark_calls(c, n->c);
        mark_calls(c, n->d);
      }
      if (n->kind == N_IF)
        mark_calls(c, n->c);
      if (n->kind == N_WHILE)
        mark_calls(c, n->else_body);
    }
  }
}
void analyze(Compiler *c) {
  int i, j;
  for (i = c->root; i; i = c->nodes[i].next)
    if (c->nodes[i].kind == N_FUNC) {
      Function *f;
      int k = c->nfunctions++;
      if (k == MAX_FUNCTIONS)
        fail(c, c->nodes[i].token, "function arena exhausted");
      f = &c->functions[k];
      f->node = i;
      f->type = c->nodes[i].type;
      f->module = c->tokens[c->nodes[i].token].module;
      if (!object_type(c->nodes[i].c))
        check_binding(c, c->nodes[i].token);
      function_name(c, i, f->name, sizeof(f->name));
      for (j = 0; j < k; ++j)
        if (f->module == c->functions[j].module &&
            !strcmp(f->name, c->functions[j].name))
          fail(c, c->nodes[i].token, "duplicate function '%s'", f->name);
      c->nodes[i].symbol = k;
      c->scope = k + 1;
      if (c->nodes[i].op)
        continue; /* Unreachable imported function: signature inference
                     deferred. */
      for (j = c->nodes[i].a; j; j = c->nodes[j].next) {
        if (f->argc == 16)
          fail(c, c->nodes[j].token, "at most 16 parameters");
        int parameter = add_symbol(c, c->nodes[j].token, c->nodes[j].type,
                                   c->nodes[j].c, -1);
        f->param[f->argc++] = parameter;
        if (object_type(c->nodes[j].type)) {
          c->symbols[parameter].reference = 1;
          object_fields(c, parameter, !(c->nodes[i].d && f->argc == 1));
        } else if (c->nodes[j].type == TY_ARRAY ||
                   c->nodes[j].type == TY_LIST) {
          c->symbols[parameter].reference = 1;
          c->symbols[parameter].value = c->nodes[j].d;
        }
      }
    }
  c->scope = 0;
  block(c, c->root);
  for (i = 0; i < c->nsymbols; ++i)
    parameter_extent(c, &c->symbols[i]);
  for (i = 0; i < c->nimports; ++i) {
    Import *im = &c->imports[i];
    char name[192];
    int module, found;
    if (im->builtin || !*im->member)
      continue;
    resolve_export(c, im, &module, name, sizeof(name));
    if (module < 0)
      continue;
    found = !*name || find_symbol(c, name, 0, module) >= 0 ||
            class_named(c, module, name) || enum_named(c, module, name);
    for (j = 0; j < c->nfunctions; ++j)
      if (c->functions[j].module == module &&
          !strcmp(c->functions[j].name, name))
        found = 1;
    if (!found)
      fail(c, im->token, "module '%s' has no export '%s'", im->name,
           im->member);
  }
  for (i = 0; i < c->nfunctions; ++i) {
    Node *n = &c->nodes[c->functions[i].node];
    if (n->op)
      continue;
    c->scope = i + 1;
    block(c, n->b);
    if (n->d)
      object_ready(c, c->functions[i].param[0], n->token);
    if (n->type != TY_VOID && !returns(c, n->b))
      fail(c, n->token, "function '%s' must return on every path",
           c->functions[i].name);
  }
  c->scope = 0;
  for (i = 0; i < c->nfunctions; ++i)
    if (!c->nodes[c->functions[i].node].op)
      visit_function(c, i, 0);
  mark_calls(c, c->root);
}

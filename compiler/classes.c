#include "pyc64.h"

int object_type(int type) {
  return type >= TY_CLASS_BASE && type < TY_CLASS_BASE + MAX_CLASSES;
}
int class_named(Compiler *c, int module, const char *name) {
  int i;
  for (i = 0; i < c->nclasses; ++i)
    if (c->classes[i].module == module && !strcmp(c->classes[i].name, name))
      return TY_CLASS_BASE + i;
  return 0;
}
int class_field(Compiler *c, int type, int token) {
  ClassLayout *layout = &c->classes[type - TY_CLASS_BASE];
  char name[64], field[64];
  int i;
  token_text(c, token, name, sizeof(name));
  for (i = 0; i < layout->nfields; ++i) {
    token_text(c, c->nodes[layout->fields[i]].token, field, sizeof(field));
    if (!strcmp(name, field))
      return i;
  }
  fail(c, token, "class '%s' has no field '%s'", layout->name, name);
  return -1;
}
int class_property(Compiler *c, int type, int token, int writing) {
  ClassLayout *layout = &c->classes[type - TY_CLASS_BASE];
  char name[64], property[64];
  int i;
  token_text(c, token, name, sizeof(name));
  for (i = 0; i < layout->nproperties; ++i) {
    token_text(c, c->nodes[layout->getters[i]].token, property,
               sizeof(property));
    if (!strcmp(name, property))
      return writing ? (layout->setters[i] ? layout->setters[i] : -1)
                     : layout->getters[i];
  }
  return 0;
}
void function_name(Compiler *c, int node, char *name, size_t size) {
  Node *n = &c->nodes[node];
  char method[64];
  token_text(c, n->token, method, sizeof(method));
  if (object_type(n->c))
    snprintf(name, size, "%s.%s%s", c->classes[n->c - TY_CLASS_BASE].name,
             method,
             n->accessor == 1   ? ".get"
             : n->accessor == 2 ? ".set"
                                : "");
  else
    snprintf(name, size, "%s", method);
}
static int resolved_type(Compiler *c, int type) {
  int node;
  if (type < TY_NAMED_BASE)
    return type;
  node = type - TY_NAMED_BASE;
  type = library_type(c, node);
  if (!type)
    fail(c, c->nodes[node].token,
         "unsupported type; import fixed-width types from pyc64.types or a "
         "class");
  return type;
}
void prepare_classes(Compiler *c) {
  int i, j, next;
  /* Register all names before resolving forward/imported class annotations. */
  for (i = c->root; i; i = c->nodes[i].next)
    if (c->nodes[i].kind == N_CLASS) {
      ClassLayout *layout;
      int module = c->tokens[c->nodes[i].token].module;
      char name[64];
      check_binding(c, c->nodes[i].token);
      token_text(c, c->nodes[i].token, name, sizeof(name));
      if (class_named(c, module, name) || enum_named(c, module, name))
        fail(c, c->nodes[i].token, "duplicate class '%s'", name);
      if (c->nclasses == MAX_CLASSES)
        fail(c, c->nodes[i].token, "class arena exhausted (limit %d)",
             MAX_CLASSES);
      layout = &c->classes[c->nclasses++];
      strcpy(layout->name, name);
      layout->node = i;
      layout->module = module;
    }
  for (i = 1; i <= c->nnodes; ++i) {
    Node *n = &c->nodes[i];
    n->type = resolved_type(c, n->type);
    if (n->kind == N_ASSIGN || n->kind == N_PARAM)
      n->c = resolved_type(c, n->c);
  }
  for (i = c->root; i; i = next) {
    Node *n = &c->nodes[i];
    char name[64];
    int module = c->tokens[n->token].module;
    next = n->next;
    if (n->kind != N_CLASS) {
      if (n->kind == N_FUNC ||
          (n->kind == N_ASSIGN && c->nodes[n->a].kind == N_NAME)) {
        token_text(c, n->kind == N_FUNC ? n->token : c->nodes[n->a].token, name,
                   sizeof(name));
        if (class_named(c, module, name) || enum_named(c, module, name))
          fail(c, n->token, "declaration conflicts with class '%s'", name);
      }
      continue;
    }
    int type;
    /* Find by node rather than depending on a parsed class node's type. */
    for (j = 0; j < c->nclasses && c->classes[j].node != i; ++j) {
    }
    type = TY_CLASS_BASE + j;
    ClassLayout *layout = &c->classes[j];
    int body = n->a, tail = i;
    n->a = 0;
    n->type = type;
    while (body) {
      Node *member = &c->nodes[body];
      int following = member->next;
      member->next = 0;
      if (member->kind == N_FUNC) {
        int self = member->a;
        token_text(c, member->token, name, sizeof(name));
        if (!self)
          fail(c, member->token, "methods require a self parameter");
        char self_name[64];
        token_text(c, c->nodes[self].token, self_name, sizeof(self_name));
        if (strcmp(self_name, "self") ||
            (c->nodes[self].type && c->nodes[self].type != type))
          fail(c, c->nodes[self].token,
               "first method parameter must be self of its class type");
        c->nodes[self].type = type;
        member->c = type;
        if (member->accessor) {
          int parameters = 0, parameter;
          if (!strcmp(name, "__init__"))
            fail(c, member->token, "__init__ cannot be a property");
          for (parameter = member->a; parameter;
               parameter = c->nodes[parameter].next)
            ++parameters;
          if (parameters != member->accessor)
            fail(c, member->token,
                 "property getter requires self; setter requires self and "
                 "value");
          if (member->accessor == 1) {
            if (class_property(c, type, member->token, 0))
              fail(c, member->token, "duplicate property '%s'", name);
            if (layout->nproperties == MAX_FIELDS)
              fail(c, member->token, "at most %d properties per class",
                   MAX_FIELDS);
            layout->getters[layout->nproperties++] = body;
          } else {
            int property;
            for (property = 0; property < layout->nproperties; ++property) {
              char getter[64];
              token_text(c, c->nodes[layout->getters[property]].token, getter,
                         sizeof(getter));
              if (!strcmp(getter, name))
                break;
            }
            if (property == layout->nproperties)
              fail(c, member->token,
                   "setter requires a preceding @property getter");
            if (layout->setters[property])
              fail(c, member->token, "duplicate property setter '%s'", name);
            if (member->type && member->type != TY_VOID)
              fail(c, member->token, "property setter must return None");
            member->type = TY_VOID;
            layout->setters[property] = body;
          }
        }
        member->d = !strcmp(name, "__init__");
        if (member->d && member->type && member->type != TY_VOID)
          fail(c, member->token, "__init__ must return None");
        if (member->d)
          member->type = TY_VOID;
        c->nodes[tail].next = body;
        tail = body;
      } else if (member->kind == N_ASSIGN &&
                 c->nodes[member->a].kind == N_NAME && !member->op &&
                 scalar_type(member->type)) {
        token_text(c, c->nodes[member->a].token, name, sizeof(name));
        for (j = 0; j < layout->nfields; ++j) {
          char other[64];
          token_text(c, c->nodes[layout->fields[j]].token, other,
                     sizeof(other));
          if (!strcmp(name, other))
            fail(c, member->token, "duplicate field '%s'", name);
        }
        if (layout->nfields == MAX_FIELDS)
          fail(c, member->token, "at most %d fields per class", MAX_FIELDS);
        j = layout->nfields++;
        layout->fields[j] = body;
        layout->offsets[j] = layout->size;
        layout->size += type_size(member->type);
      } else if (member->kind != N_PASS)
        fail(c, member->token,
             "class bodies require annotated scalar fields and methods");
      body = following;
    }
    for (body = n->next; body && body != next; body = c->nodes[body].next) {
      token_text(c, c->nodes[body].token, name, sizeof(name));
      if (!c->nodes[body].accessor &&
          class_property(c, type, c->nodes[body].token, 0))
        fail(c, c->nodes[body].token, "method conflicts with property '%s'",
             name);
      for (j = 0; j < layout->nfields; ++j) {
        char field[64];
        token_text(c, c->nodes[layout->fields[j]].token, field, sizeof(field));
        if (!strcmp(name, field))
          fail(c, c->nodes[body].token, "method conflicts with field '%s'",
               name);
      }
    }
    c->nodes[tail].next = next;
  }
}

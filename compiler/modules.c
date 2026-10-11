#include "pyc64.h"

/* Modules share the bounded source/token/node arenas; files are retained once
   for diagnostics, and imports have no target-side Python runtime machinery. */
static void module_path(Compiler *c, Import *im, char *result) {
  char relative[128], directory[MAX_PATH];
  const char *slash = strrchr(c->modules[0].path, '/');
  const char *back = strrchr(c->modules[0].path, '\\');
  int i, search, pass;
  size_t length = strlen(im->name);
  if (back && (!slash || back > slash))
    slash = back;
  if (slash) {
    size_t n = (size_t)(slash - c->modules[0].path) + 1;
    memcpy(directory, c->modules[0].path, n);
    directory[n] = 0;
  } else
    strcpy(directory, ".");
  for (i = 0; i < (int)length; ++i)
    relative[i] = im->name[i] == '.' ? '/' : im->name[i];
  strcpy(relative + length, ".py");
  /* Keep a user math.py's normal precedence. The bundled implementation lives
     under pyc64 so adding stdlib to CPython's path never shadows host math. */
  for (pass = 0; pass < (!strcmp(im->name, "math") ? 2 : 1); ++pass) {
    if (pass)
      strcpy(relative, "pyc64/_math_library.py");
    for (search = -1; search <= c->nsearch_paths; ++search) {
      const char *root = search < 0                  ? directory
                         : search < c->nsearch_paths ? c->search_paths[search]
                                                     : "stdlib";
      size_t root_length = strlen(root), relative_length = strlen(relative);
      if (root_length + relative_length + 2 >= MAX_PATH)
        fail(c, im->token, "module path is too long");
      memcpy(result, root, root_length);
      result[root_length] = '/';
      memcpy(result + root_length + 1, relative, relative_length + 1);
      if (platform_exists(result))
        return;
    }
  }
  fail(c, im->token, "cannot find module '%s'; add its directory with -I",
       im->name);
}
static void order_module(Compiler *c, int module, int token, int *head,
                         int *tail) {
  Module *m = &c->modules[module];
  int i;
  if (m->state == 2)
    return;
  if (m->state == 1)
    fail(c, token, "circular import involving module '%s'", m->name);
  m->state = 1;
  for (i = 0; i < c->nimports; ++i) {
    Import *im = &c->imports[i];
    if (im->module == module && !im->builtin)
      order_module(c, im->destination, im->token, head, tail);
  }
  if (m->root) {
    if (*tail)
      c->nodes[*tail].next = m->root;
    else
      *head = m->root;
    *tail = m->tail;
  }
  m->state = 2;
}
void load_program(Compiler *c, const char *path) {
  int i, j, head = 0, tail = 0;
  if (strlen(path) >= MAX_PATH)
    fail(c, 0, "source path is too long");
  c->nmodules = 1;
  c->nbytes = 2; /* Shared immutable empty bytes value, with a length header. */
  strcpy(c->modules[0].path, path);
  strcpy(c->modules[0].name, "__main__");
  for (i = 0; i < c->nmodules; ++i) {
    Module *m = &c->modules[i];
    c->active_module = i;
    if (!platform_read(c, m->path))
      fail(c, m->import_token, "unable to load source '%s'", m->path);
    m->first_token = c->ntokens;
    lex(c);
    c->cursor = m->first_token;
    c->root = 0;
    parse(c);
    m->root = c->root;
    for (j = m->root; j; j = c->nodes[j].next)
      m->tail = j;
    for (j = 0; j < c->nimports; ++j) {
      Import *im = &c->imports[j];
      char resolved[MAX_PATH];
      int k;
      if (im->module != i || im->builtin)
        continue;
      module_path(c, im, resolved);
      for (k = 0; k < c->nmodules; ++k)
        if (platform_same_file(c->modules[k].path, resolved))
          break;
      if (k == c->nmodules) {
        if (k == MAX_MODULES)
          fail(c, im->token, "module arena exhausted (limit %d)", MAX_MODULES);
        strcpy(c->modules[k].path, resolved);
        strcpy(c->modules[k].name, im->name);
        c->modules[k].import_token = im->token;
        ++c->nmodules;
      }
      im->destination = k;
    }
  }
  order_module(c, 0, 0, &head, &tail);
  c->root = head;
  c->active_module = 0;
}
static void qualified(Compiler *c, int i, char *out, size_t cap, int depth) {
  Node *n = &c->nodes[i];
  char part[64];
  if (depth > MAX_DEPTH)
    fail(c, n->token, "qualified name is too deeply nested");
  if (n->kind == N_ATTR) {
    qualified(c, n->a, out, cap, depth + 1);
    token_text(c, n->token, part, sizeof(part));
    if (strlen(out) + strlen(part) + 2 >= cap)
      fail(c, n->token, "qualified name too long");
    strcat(out, ".");
    strcat(out, part);
  } else if (n->kind == N_NAME)
    token_text(c, n->token, out, cap);
  else
    fail(c, n->token, "expected a named function or hardware property");
}
static void resolve_raw(Compiler *c, int owner, const char *raw, int token,
                        int *module, char *name, size_t size, int depth) {
  int j;
  if (depth > MAX_MODULES)
    fail(c, token, "import alias chain is too deep");
  for (j = 0; j < c->nimports; ++j) {
    Import *im = &c->imports[j];
    size_t k = strlen(im->alias);
    char replacement[192];
    const char *suffix;
    if (im->module != owner || strncmp(raw, im->alias, k) ||
        (raw[k] && raw[k] != '.'))
      continue;
    suffix = raw + k;
    if (im->builtin) {
      if (strlen(im->target) + strlen(suffix) >= size)
        fail(c, token, "resolved name too long");
      strcpy(name, im->target);
      strcat(name, suffix);
      *module = -1;
      return;
    }
    if (*im->member) {
      if (strlen(im->member) + strlen(suffix) >= sizeof(replacement))
        fail(c, token, "resolved name too long");
      strcpy(replacement, im->member);
      strcat(replacement, suffix);
    } else {
      if (!*suffix) {
        *name =
            0; /* A source module re-export; values still require a member. */
        *module = im->destination;
        return;
      }
      ++suffix;
      if (*im->prefix) {
        size_t p = strlen(im->prefix);
        if (!strcmp(suffix, im->prefix)) {
          *name = 0;
          *module = im->destination;
          return;
        }
        if (strncmp(suffix, im->prefix, p) || suffix[p] != '.')
          fail(c, token, "expected '%s.%s.member'", im->alias, im->prefix);
        suffix += p + 1;
      }
      if (strlen(suffix) >= sizeof(replacement))
        fail(c, token, "resolved name too long");
      strcpy(replacement, suffix);
    }
    resolve_raw(c, im->destination, replacement, token, module, name, size,
                depth + 1);
    return;
  }
  if (strlen(raw) >= size)
    fail(c, token, "resolved name too long");
  strcpy(name, raw);
  *module = owner;
}
void resolve_name(Compiler *c, int node, int *module, char *name, size_t size) {
  char raw[192];
  Node *n = &c->nodes[node];
  qualified(c, node, raw, sizeof(raw), 0);
  resolve_raw(c, c->tokens[n->token].module, raw, n->token, module, name, size,
              0);
}
int named_expression(Compiler *c, int node) {
  int root = node, depth = 0;
  while (c->nodes[root].kind == N_ATTR && ++depth <= MAX_DEPTH)
    root = c->nodes[root].a;
  return c->nodes[root].kind == N_NAME && depth <= MAX_DEPTH;
}
int module_namespace(Compiler *c, int node) {
  char raw[192], name[192];
  int j, owner = c->tokens[c->nodes[node].token].module;
  if (!named_expression(c, node))
    return 0;
  qualified(c, node, raw, sizeof(raw), 0);
  for (j = 0; j < c->nimports; ++j) {
    Import *im = &c->imports[j];
    if (im->module != owner || *im->member)
      continue;
    snprintf(name, sizeof(name), "%s%s%s", im->alias, *im->prefix ? "." : "",
             im->prefix);
    if (!strcmp(raw, name) || !strcmp(raw, im->alias))
      return 1;
  }
  resolve_name(c, node, &j, name, sizeof(name));
  if (j >= 0 && !*name)
    return 1;
  if (j < 0) {
    static const char *hardware_namespaces[] = {"c64",
                                                "c64.video",
                                                "c64.video.screen",
                                                "c64.input",
                                                "c64.input.joystick",
                                                "c64.input.joystick.port1",
                                                "c64.input.joystick.port2",
                                                "c64.memory",
                                                "c64.keyboard",
                                                "c64.sprite",
                                                "c64.kernal",
                                                NULL};
    int k;
    for (k = 0; hardware_namespaces[k]; ++k)
      if (!strcmp(name, hardware_namespaces[k]))
        return 1;
  }
  return 0;
}
void resolve_export(Compiler *c, Import *im, int *module, char *name,
                    size_t size) {
  resolve_raw(c, im->destination, im->member, im->token, module, name, size, 0);
}
int library_type(Compiler *c, int node) {
  char name[192];
  int module, i;
  if (!named_expression(c, node))
    return TY_NONE;
  resolve_name(c, node, &module, name, sizeof(name));
  if (module >= 0) {
    i = enum_named(c, module, name);
    if (i)
      return i;
    i = class_named(c, module, name);
    if (i)
      return i;
    if (!strcmp(name, "bool"))
      return TY_BOOL;
    if (!strcmp(name, "str"))
      return TY_STR;
    if (!strcmp(name, "bytes"))
      return TY_BYTES;
    if (!strcmp(name, "float"))
      return TY_FLOAT;
    if (!strcmp(name, "int"))
      return TY_I16;
    if (!strcmp(name, "list"))
      return TY_LIST;
    if (!strcmp(name, "None"))
      return TY_VOID;
    return TY_NONE;
  }
  if (!strncmp(name, "pyc64.types.", 12)) {
    for (i = TY_U8; i <= TY_I16; ++i)
      if (!strcmp(name + 12, type_name(i)))
        return i;
    if (!strcmp(name + 12, "char"))
      return TY_U8;
    if (!strcmp(name + 12, "array"))
      return TY_ARRAY;
  }
  return TY_NONE;
}
void check_binding(Compiler *c, int token) {
  char name[64];
  int j, module = c->tokens[token].module;
  static const char *reserved[] = {
      "print", "peek",  "poke",  "len",  "bool",  "u8",   "i8",  "u16",
      "i16",   "str",   "array", "char", "range", "list", "ord", "chr",
      "input", "float", "pow",   "int",  "bytes", NULL};
  token_text(c, token, name, sizeof(name));
  for (j = 0; reserved[j]; ++j)
    if (!strcmp(name, reserved[j]))
      fail(c, token, "'%s' is a reserved compiler builtin", name);
  for (j = 0; j < c->nimports; ++j)
    if (c->imports[j].module == module && !strcmp(c->imports[j].alias, name))
      fail(c, token, "declaration conflicts with import alias '%s'", name);
}

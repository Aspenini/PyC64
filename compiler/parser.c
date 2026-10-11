#include "pyc64.h"

static int kind(Compiler *c) { return c->tokens[c->cursor].kind; }
static int accept(Compiler *c, int k) {
  if (kind(c) != k)
    return 0;
  ++c->cursor;
  return 1;
}
static int word(Compiler *c, const char *s) {
  Token *t = &c->tokens[c->cursor];
  return t->kind == T_NAME && t->length == (int)strlen(s) &&
         !strncmp(c->source + t->start, s, (size_t)t->length);
}
static int takeword(Compiler *c, const char *s) {
  if (!word(c, s))
    return 0;
  ++c->cursor;
  return 1;
}
static int expect(Compiler *c, int k, const char *message) {
  int t = c->cursor;
  if (!accept(c, k))
    fail(c, t, "%s", message);
  return t;
}
static void valid_name(Compiler *c, int token) {
  static const char *keywords[] = {
      "False",  "None",     "True",  "and",    "as",       "assert",
      "async",  "await",    "break", "class",  "continue", "def",
      "del",    "elif",     "else",  "except", "finally",  "for",
      "from",   "global",   "if",    "import", "in",       "is",
      "lambda", "nonlocal", "not",   "or",     "pass",     "raise",
      "return", "try",      "while", "with",   "yield",    NULL};
  char name[64];
  int i;
  token_text(c, token, name, sizeof(name));
  for (i = 0; keywords[i]; ++i)
    if (!strcmp(name, keywords[i]))
      fail(c, token, "Python keyword '%s' cannot be used as an identifier",
           name);
}
static int identifier(Compiler *c, const char *message) {
  int token = expect(c, T_NAME, message);
  valid_name(c, token);
  return token;
}
int node_new(Compiler *c, int k, int t) {
  int i = ++c->nnodes;
  if (i >= MAX_NODES)
    fail(c, t, "syntax arena exhausted (limit %d)", MAX_NODES);
  c->nodes[i].kind = k;
  c->nodes[i].token = t;
  c->nodes[i].symbol = -1;
  return i;
}
static int expression(Compiler *c, int min);
static int statement(Compiler *c);
static int annotation_name(Compiler *c) {
  int n = node_new(c, N_NAME, identifier(c, "expected type name"));
  while (accept(c, '.')) {
    int attr = node_new(c, N_ATTR, identifier(c, "expected type member"));
    c->nodes[attr].a = n;
    n = attr;
  }
  return n;
}
static int primitive(Compiler *c) {
  if (takeword(c, "None"))
    return TY_VOID;
  int n = annotation_name(c), type = library_type(c, n);
  if (!type)
    return TY_NAMED_BASE + n;
  return type;
}
static int atom(Compiler *c) {
  int t = c->cursor, n = 0;
  if (++c->depth > MAX_DEPTH)
    fail(c, t, "expression nesting exceeds %d", MAX_DEPTH);
  if (accept(c, T_NUMBER)) {
    n = node_new(c, N_CONST, t);
    c->nodes[n].value = c->tokens[t].value;
  } else if (accept(c, T_FLOAT)) {
    n = node_new(c, N_FLOAT, t);
    c->nodes[n].type = TY_FLOAT;
    c->nodes[n].real = c->tokens[t].real;
  } else if (word(c, "True") || word(c, "False")) {
    n = node_new(c, N_CONST, t);
    c->nodes[n].value = takeword(c, "True");
    if (!c->nodes[n].value)
      ++c->cursor;
    c->nodes[n].type = TY_BOOL;
  } else if (takeword(c, "None")) {
    n = node_new(c, N_NONE, t);
    c->nodes[n].type = TY_VOID;
  } else if (accept(c, T_STRING)) {
    Token *tok = &c->tokens[t];
    int p = tok->start + 1, end = tok->start + tok->length - 1,
        start = c->nstrings;
    n = node_new(c, N_STRING, t);
    while (p < end) {
      unsigned char ch = (unsigned char)c->source[p++];
      if (ch == '\\') {
        ch = (unsigned char)c->source[p++];
        switch (ch) {
        case 'n':
          ch = 10;
          break;
        case 'r':
          ch = 13;
          break;
        case 't':
          ch = 9;
          break;
        case 'b':
          ch = 8;
          break;
        case 'f':
          ch = 12;
          break;
        case 'v':
          ch = 11;
          break;
        case '\\':
        case '\'':
        case '"':
          break;
        default:
          fail(c, t, "unsupported string escape");
        }
      }
      if (ch == 0 || ch > 127)
        fail(c, t, "strings support ASCII characters only");
      if (c->nstrings + 1 >= MAX_STRINGS)
        fail(c, t, "string arena exhausted");
      c->strings[c->nstrings++] = (char)ch;
    }
    c->strings[c->nstrings++] = 0;
    c->nodes[n].value = start;
    c->nodes[n].d = c->nstrings - start - 1;
  } else if (accept(c, T_BYTES)) {
    n = parse_bytes(c, t);
  } else if (!word(c, "not") && accept(c, T_NAME)) {
    valid_name(c, t);
    n = node_new(c, N_NAME, t);
  } else if (accept(c, '[')) {
    int first = 0, last = 0;
    n = node_new(c, N_LIST, t);
    if (kind(c) != ']')
      do {
        int item = expression(c, 0);
        if (last)
          c->nodes[last].next = item;
        else
          first = item;
        last = item;
        ++c->nodes[n].d;
      } while (accept(c, ',') && kind(c) != ']');
    expect(c, ']', "expected ']' after list elements");
    c->nodes[n].a = first;
  } else if (accept(c, '(')) {
    n = expression(c, 0);
    expect(c, ')', "expected ')'");
  } else if (kind(c) == '-' || kind(c) == '+' || kind(c) == '~' ||
             word(c, "not")) {
    int op = kind(c);
    if (takeword(c, "not"))
      op = OP_NOT;
    else
      ++c->cursor;
    n = node_new(c, N_UNARY, t);
    c->nodes[n].op = op;
    c->nodes[n].a = expression(c, op == OP_NOT ? 3 : 11);
  } else
    fail(c, t, "expected expression");
  for (;;) {
    int x;
    if (accept(c, '.')) {
      x = node_new(c, N_ATTR, identifier(c, "expected attribute name"));
      c->nodes[x].a = n;
      n = x;
    } else if (accept(c, '[')) {
      x = node_new(c, N_INDEX, t);
      c->nodes[x].a = n;
      if (kind(c) != ':')
        c->nodes[x].b = expression(c, 0);
      if (accept(c, ':')) {
        c->nodes[x].kind = N_SLICE;
        if (kind(c) != ':' && kind(c) != ']')
          c->nodes[x].c = expression(c, 0);
        if (accept(c, ':') && kind(c) != ']')
          c->nodes[x].d = expression(c, 0);
      }
      expect(c, ']', "expected ']'");
      n = x;
    } else if (accept(c, '(')) {
      int first = 0, last = 0;
      x = node_new(c, N_CALL, t);
      c->nodes[x].a = n;
      if (kind(c) != ')')
        do {
          int arg = expression(c, 0);
          if (kind(c) == '=')
            fail(c, c->cursor, "keyword arguments are not supported");
          if (last)
            c->nodes[last].next = arg;
          else
            first = arg;
          last = arg;
        } while (accept(c, ',') && kind(c) != ')');
      expect(c, ')', "expected ')' after arguments");
      c->nodes[x].b = first;
      n = x;
    } else
      break;
  }
  --c->depth;
  return n;
}
static int binary_operator(Compiler *c) {
  if (word(c, "and"))
    return OP_AND;
  if (word(c, "or"))
    return OP_OR;
  if (word(c, "in"))
    return OP_IN;
  if (word(c, "not"))
    return OP_NOTIN;
  return kind(c);
}
static int precedence(int k) {
  switch (k) {
  case OP_OR:
    return 1;
  case OP_AND:
    return 2;
  case T_EQ:
  case T_NE:
  case '<':
  case '>':
  case T_LE:
  case T_GE:
  case OP_IN:
  case OP_NOTIN:
    return 4;
  case '|':
    return 5;
  case '^':
    return 6;
  case '&':
    return 7;
  case T_SHL:
  case T_SHR:
    return 8;
  case '+':
  case '-':
    return 9;
  case '*':
  case '/':
  case T_FLOORDIV:
  case '%':
    return 10;
  case T_POWER:
    return 12;
  default:
    return 0;
  }
}
static int expression(Compiler *c, int min) {
  int left = atom(c);
  for (;;) {
    int op = binary_operator(c), prec, n, token = c->cursor;
    prec = precedence(op);
    if (!prec || prec <= min)
      break;
    ++c->cursor;
    if (op == OP_NOTIN && !takeword(c, "in"))
      fail(c, c->cursor, "expected 'in' after binary 'not'");
    if (op == T_POWER && word(c, "not"))
      fail(c, c->cursor,
           "power requires parentheses around a 'not' expression");
    n = node_new(c, N_BINARY, token);
    c->nodes[n].op = op;
    c->nodes[n].a = left;
    c->nodes[n].b = expression(c, op == T_POWER ? prec - 1 : prec);
    left = n;
    if (prec == 4 && precedence(binary_operator(c)) == 4)
      fail(c, c->cursor,
           "chained comparisons are unsupported; combine comparisons with "
           "'and'");
  }
  return left;
}
static int suite(Compiler *c) {
  int first = 0, last = 0;
  expect(c, ':', "expected ':'");
  expect(c, T_NEWLINE, "expected newline after ':'");
  expect(c, T_INDENT, "expected indented block");
  if (++c->depth > MAX_DEPTH)
    fail(c, c->cursor, "block nesting exceeds %d", MAX_DEPTH);
  while (kind(c) != T_DEDENT && kind(c) != T_EOF) {
    int n = statement(c);
    if (n) {
      if (last)
        c->nodes[last].next = n;
      else
        first = n;
      last = n;
    }
  }
  --c->depth;
  expect(c, T_DEDENT, "expected end of block");
  return first;
}
static int if_statement(Compiler *c, int t) {
  int n = node_new(c, N_IF, t);
  c->nodes[n].a = expression(c, 0);
  c->nodes[n].b = suite(c);
  if (takeword(c, "elif"))
    c->nodes[n].c = if_statement(c, c->cursor - 1);
  else if (takeword(c, "else"))
    c->nodes[n].c = suite(c);
  return n;
}
static void module_name(Compiler *c, char *module, size_t size) {
  char part[64];
  *module = 0;
  do {
    token_text(c, identifier(c, "expected module name"), part, sizeof(part));
    if (strlen(module) + strlen(part) + 2 >= size)
      fail(c, c->cursor - 1, "module name is too long");
    if (*module)
      strcat(module, ".");
    strcat(module, part);
  } while (accept(c, '.'));
}
static void validate_import(Compiler *c, Import *im) {
  int i;
  for (i = 0; i < c->nimports - 1; ++i)
    if (c->imports[i].module == im->module &&
        !strcmp(c->imports[i].alias, im->alias))
      fail(c, im->token, "duplicate import alias '%s'", im->alias);
  /* Hardware libraries written in the Python subset use the normal loader. */
  if ((!strcmp(im->name, "pyc64") || !strcmp(im->name, "c64")) &&
      !strcmp(im->member, "assets")) {
    strcat(im->name, ".assets");
    *im->member = 0;
  }
  if (!strcmp(im->name, "pyc64.assets") || !strcmp(im->name, "c64.assets")) {
    if (*im->member &&
        (strcmp(im->name, "pyc64.assets") || strcmp(im->member, "load")) &&
        (strcmp(im->name, "c64.assets") ||
         (strcmp(im->member, "sprite") &&
          strcmp(im->member, "sprite_multicolor") &&
          strcmp(im->member, "charset") && strcmp(im->member, "tileset") &&
          strcmp(im->member, "tilemap") && strcmp(im->member, "bitmap") &&
          strcmp(im->member, "bitmap_screen") && strcmp(im->member, "sid") &&
          strcmp(im->member, "address"))))
      fail(c, im->token, "unknown asset export '%s'", im->member);
    im->builtin = 1;
    return;
  }
  if (!strcmp(im->name, "c64") && !strcmp(im->member, "tiles")) {
    strcpy(im->name, "c64.tiles");
    *im->member = 0;
  }
  if (!strcmp(im->name, "c64.tiles"))
    return;
  if (!strcmp(im->name, "c64") && !strcmp(im->member, "bitmap")) {
    strcpy(im->name, "c64.bitmap");
    *im->member = 0;
  }
  if (!strcmp(im->name, "c64.bitmap"))
    return;
  if (!strcmp(im->name, "c64") && !strcmp(im->member, "music")) {
    strcpy(im->name, "c64.music");
    *im->member = 0;
  }
  if (!strcmp(im->name, "c64.music"))
    return;
  if (!strcmp(im->name, "c64._music")) {
    if (*im->member && strcmp(im->member, "start") &&
        strcmp(im->member, "stop") && strcmp(im->member, "active") &&
        strcmp(im->member, "ticks"))
      fail(c, im->token, "unknown c64._music export '%s'", im->member);
    im->builtin = 1;
    return;
  }
  if (!strcmp(im->name, "c64._video")) {
    if (*im->member && strcmp(im->member, "mode"))
      fail(c, im->token, "unknown c64._video export '%s'", im->member);
    im->builtin = 1;
    return;
  }
  if (!strcmp(im->name, "c64") && !strcmp(im->member, "irq")) {
    strcpy(im->name, "c64.irq");
    *im->member = 0;
  }
  if (!strcmp(im->name, "c64.irq")) {
    if (*im->member && strcmp(im->member, "on_raster") &&
        strcmp(im->member, "set_line") && strcmp(im->member, "off") &&
        strcmp(im->member, "active") && strcmp(im->member, "disable") &&
        strcmp(im->member, "restore"))
      fail(c, im->token, "unknown c64.irq export '%s'", im->member);
    im->builtin = 1;
    return;
  }
  if (!strcmp(im->name, "enum")) {
    if (*im->member && strcmp(im->member, "IntEnum") &&
        strcmp(im->member, "auto"))
      fail(c, im->token, "only enum.IntEnum and enum.auto are supported");
    im->builtin = 1;
    return;
  }
  if (!strcmp(im->name, "c64") &&
      (!strcmp(im->member, "sprite") || !strcmp(im->member, "sid") ||
       !strcmp(im->member, "audio") || !strcmp(im->member, "disk") ||
       !strcmp(im->member, "reu"))) {
    strcpy(im->name, !strcmp(im->member, "sprite")  ? "c64.sprite"
                     : !strcmp(im->member, "sid")   ? "c64.sid"
                     : !strcmp(im->member, "audio") ? "c64.audio"
                     : !strcmp(im->member, "disk")  ? "c64.disk"
                                                    : "c64.reu");
    *im->member = 0;
  }
  if (!strcmp(im->name, "c64.reu") || !strcmp(im->name, "c64._reu")) {
    if (!c->target)
      fail(c, im->token,
           "module '%s' requires target 'c64-reu' or 'c64-ultimate'", im->name);
    if (!strcmp(im->name, "c64._reu")) {
      if (*im->member && strcmp(im->member, "present") &&
          strcmp(im->member, "transfer"))
        fail(c, im->token, "unknown c64._reu export '%s'", im->member);
      im->builtin = 1;
    }
    return;
  }
  if (!strcmp(im->name, "c64.sid") || !strcmp(im->name, "c64.audio") ||
      !strcmp(im->name, "c64.disk") || !strcmp(im->name, "c64.sprite") ||
      !strcmp(im->name, "c64._sid") || !strcmp(im->name, "c64._sid_voice"))
    return;
  /* The source sprite library re-exports these native primitives. */
  if (!strcmp(im->name, "pyc64._math")) {
    static const char *exports[] = {"pow",  "sqrt",  "exp",  "log",  "sin",
                                    "cos",  "tan",   "atan", "fabs", "floor",
                                    "ceil", "trunc", NULL};
    if (*im->member) {
      for (i = 0; exports[i] && strcmp(exports[i], im->member); ++i)
        ;
      if (!exports[i])
        fail(c, im->token, "unknown pyc64._math export '%s'", im->member);
    }
    im->builtin = 1;
    return;
  }
  if (!strcmp(im->name, "c64._sprite")) {
    if (*im->member && strcmp(im->member, "enable") &&
        strcmp(im->member, "disable") && strcmp(im->member, "position") &&
        strcmp(im->member, "color") && strcmp(im->member, "data"))
      fail(c, im->token, "unknown c64._sprite export '%s'", im->member);
    im->builtin = 1;
    strcpy(im->target, "c64.sprite");
    if (*im->member) {
      strcat(im->target, ".");
      strcat(im->target, im->member);
    }
    return;
  }
  if (!strcmp(im->name, "typing") || !strcmp(im->name, "pyc64.types")) {
    im->builtin = 1;
    if (*im->member) {
      if (!strcmp(im->name, "typing")) {
        if (strcmp(im->member, "Final"))
          fail(c, im->token, "only typing.Final is supported");
      } else if (strcmp(im->member, "u8") && strcmp(im->member, "i8") &&
                 strcmp(im->member, "u16") && strcmp(im->member, "i16") &&
                 strcmp(im->member, "char") && strcmp(im->member, "array"))
        fail(c, im->token, "unknown pyc64.types export '%s'", im->member);
    }
  }
  if (!strcmp(im->name, "c64ultimate") ||
      !strncmp(im->name, "c64ultimate.", 12))
    fail(c, im->token,
         c->target == 2 ? "Ultimate APIs are not implemented yet"
                        : "module '%s' requires target 'c64-ultimate'",
         im->name);
  if (!strcmp(im->name, "c64") || !strncmp(im->name, "c64.", 4)) {
    im->builtin = 1;
    if (*im->member) {
      if (strcmp(im->target, "c64.video.screen") &&
          strcmp(im->target, "c64.input.joystick") &&
          strcmp(im->target, "c64.memory.peek") &&
          strcmp(im->target, "c64.memory.poke") &&
          strcmp(im->target, "c64.keyboard.read") &&
          strcmp(im->target, "c64.keyboard.clear") &&
          strcmp(im->target, "c64.sprite.enable") &&
          strcmp(im->target, "c64.sprite.disable") &&
          strcmp(im->target, "c64.sprite.position") &&
          strcmp(im->target, "c64.sprite.color") &&
          strcmp(im->target, "c64.sprite.data") &&
          strcmp(im->target, "c64.kernal.open") &&
          strcmp(im->target, "c64.kernal.close") &&
          strcmp(im->target, "c64.kernal.read_byte") &&
          strcmp(im->target, "c64.kernal.write_byte") &&
          strcmp(im->target, "c64.kernal.write_text") &&
          strcmp(im->target, "c64.kernal.status") &&
          strcmp(im->target, "c64.kernal.error") &&
          strcmp(im->target, "c64.kernal") &&
          strcmp(im->target, "c64.keyboard") &&
          strcmp(im->target, "c64.sprite") && strcmp(im->target, "c64.video") &&
          strcmp(im->target, "c64.input") && strcmp(im->target, "c64.memory"))
        fail(c, im->token, "module export '%s' is not supported yet",
             im->target);
    } else if (strcmp(im->name, "c64") && strcmp(im->name, "c64.video") &&
               strcmp(im->name, "c64.input") &&
               strcmp(im->name, "c64.memory") &&
               strcmp(im->name, "c64.keyboard") &&
               strcmp(im->name, "c64.sprite") && strcmp(im->name, "c64.kernal"))
      fail(c, im->token, "module '%s' is not supported yet", im->name);
  }
}
static void imported(Compiler *c, int from) {
  char module[96] = "", member[64];
  int t = c->cursor - 1;
  if (c->scope || c->depth)
    fail(c, t, "imports must be at module scope");
  if (from) {
    module_name(c, module, sizeof(module));
    if (!takeword(c, "import"))
      fail(c, c->cursor, "expected 'import'");
  }
  do {
    Import *im;
    const char *dot;
    if (c->nimports == MAX_IMPORTS)
      fail(c, t, "too many imports");
    im = &c->imports[c->nimports++];
    im->module = c->active_module;
    im->token = t;
    im->destination = -1;
    if (from) {
      strcpy(im->name, module);
      token_text(c, identifier(c, "expected imported name"), member,
                 sizeof(member));
      strcpy(im->member, member);
      strcpy(im->alias, im->member);
      strcpy(im->target, module);
      strcat(im->target, ".");
      strcat(im->target, member);
    } else {
      module_name(c, im->name, sizeof(im->name));
      dot = strchr(im->name, '.');
      if (dot) {
        size_t n = (size_t)(dot - im->name);
        memcpy(im->alias, im->name, n);
        im->alias[n] = 0;
        strcpy(im->prefix, dot + 1);
      } else {
        if (strlen(im->name) >= sizeof(im->alias))
          fail(c, t, "module alias is too long");
        strcpy(im->alias, im->name);
      }
      strcpy(im->target, im->alias);
    }
    if (takeword(c, "as")) {
      token_text(c, identifier(c, "expected alias"), im->alias,
                 sizeof(im->alias));
      *im->prefix = 0;
      if (!from)
        strcpy(im->target, im->name);
    }
    validate_import(c, im);
  } while (accept(c, ','));
  expect(c, T_NEWLINE, "expected newline after import");
}
static int statement(Compiler *c) {
  int t = c->cursor, n;
  if (accept(c, '@')) {
    char name[64], method[64];
    int decorator = identifier(c, "expected property decorator"), accessor = 1;
    if (!c->parsing_class || c->scope)
      fail(c, t, "property decorators require a class method");
    token_text(c, decorator, name, sizeof(name));
    if (accept(c, '.')) {
      if (!takeword(c, "setter"))
        fail(c, t, "only @property and @name.setter are supported");
      accessor = 2;
    } else if (strcmp(name, "property"))
      fail(c, t, "only @property and @name.setter are supported");
    expect(c, T_NEWLINE, "expected newline after property decorator");
    if (!word(c, "def"))
      fail(c, c->cursor, "property decorator must precede a method definition");
    n = statement(c);
    c->nodes[n].accessor = accessor;
    token_text(c, c->nodes[n].token, method, sizeof(method));
    if (accessor == 2 && strcmp(name, method))
      fail(c, decorator, "setter must use the same name as its property");
    return n;
  }
  if (takeword(c, "from")) {
    imported(c, 1);
    return node_new(c, N_PASS, t);
  }
  if (takeword(c, "import")) {
    imported(c, 0);
    return node_new(c, N_PASS, t);
  }
  if (takeword(c, "class")) {
    if (kind(c) == '=')
      fail(c, t, "Python keyword 'class' cannot be used as an identifier");
    if (c->scope || c->depth)
      fail(c, t, "classes must be defined at module scope");
    n = node_new(c, N_CLASS, identifier(c, "expected class name"));
    if (accept(c, '(')) {
      c->nodes[n].kind = N_ENUMDEF;
      c->nodes[n].c = annotation_name(c);
      expect(c, ')', "only one IntEnum base is supported");
    }
    ++c->parsing_class;
    c->nodes[n].a = suite(c);
    --c->parsing_class;
    return n;
  }
  if (takeword(c, "def")) {
    int first = 0, last = 0;
    if (c->scope)
      fail(c, t, "nested functions are unsupported");
    n = node_new(c, N_FUNC, identifier(c, "expected function name"));
    c->scope = 1;
    expect(c, '(', "expected '('");
    if (kind(c) != ')')
      do {
        int p = node_new(c, N_PARAM, identifier(c, "expected parameter name"));
        if (accept(c, ':'))
          c->nodes[p].type = primitive(c);
        if (c->nodes[p].type == TY_ARRAY || c->nodes[p].type == TY_LIST) {
          int type = c->nodes[p].type;
          expect(c, '[',
                 type == TY_ARRAY ? "expected array[T, N]"
                                  : "expected list[T]");
          c->nodes[p].c = primitive(c);
          if (c->nodes[p].c == TY_ARRAY || c->nodes[p].c == TY_LIST ||
              c->nodes[p].c == TY_VOID ||
              (type == TY_ARRAY && c->nodes[p].c == TY_STR))
            fail(c, t, "container parameters require scalar element types");
          if (type == TY_ARRAY && accept(c, ',')) {
            c->nodes[p].d = expression(c, 0);
          }
          expect(c, ']', "expected ']' after parameter element type");
        }
        if (c->nodes[p].type == TY_VOID)
          fail(c, t, "unsupported parameter type");
        if (last)
          c->nodes[last].next = p;
        else
          first = p;
        last = p;
      } while (accept(c, ',') && kind(c) != ')');
    expect(c, ')', "expected ')'");
    c->nodes[n].a = first;
    c->nodes[n].type = accept(c, T_ARROW) ? primitive(c) : TY_NONE;
    if (c->nodes[n].type == TY_ARRAY || c->nodes[n].type == TY_LIST)
      fail(c, t, "array return values are unsupported");
    c->nodes[n].b = suite(c);
    c->scope = 0;
    return n;
  }
  if (takeword(c, "if"))
    return if_statement(c, t);
  if (takeword(c, "while")) {
    n = node_new(c, N_WHILE, t);
    c->nodes[n].a = expression(c, 0);
    c->nodes[n].b = suite(c);
    if (takeword(c, "else"))
      c->nodes[n].else_body = suite(c);
    return n;
  }
  if (takeword(c, "for")) {
    n = node_new(c, N_FOR, t);
    c->nodes[n].a =
        node_new(c, N_NAME, identifier(c, "expected loop variable"));
    if (!takeword(c, "in"))
      fail(c, c->cursor, "expected 'in' after loop variable");
    if (takeword(c, "range")) {
      expect(c, '(', "expected '('");
      c->nodes[n].b = expression(c, 0);
      if (accept(c, ',') && kind(c) != ')') {
        c->nodes[n].c = expression(c, 0);
        if (accept(c, ',') && kind(c) != ')')
          c->nodes[n].d = expression(c, 0);
      }
      accept(c, ',');
      expect(c, ')', "expected ')'");
    } else {
      c->nodes[n].kind = N_EACH;
      c->nodes[n].b = expression(c, 0);
    }
    c->nodes[n].value = suite(c);
    if (takeword(c, "else"))
      c->nodes[n].else_body = suite(c);
    return n;
  }
  if (takeword(c, "assert")) {
    n = node_new(c, N_ASSERT, t);
    c->nodes[n].a = expression(c, 0);
    if (accept(c, ','))
      c->nodes[n].b = expression(c, 0);
  } else if (takeword(c, "return")) {
    n = node_new(c, N_RETURN, t);
    if (kind(c) != T_NEWLINE) {
      c->nodes[n].a = expression(c, 0);
      if (c->nodes[c->nodes[n].a].kind == N_NONE)
        c->nodes[n].a = 0;
    }
  } else if (takeword(c, "break"))
    n = node_new(c, N_BREAK, t);
  else if (takeword(c, "continue"))
    n = node_new(c, N_CONTINUE, t);
  else if (takeword(c, "pass"))
    n = node_new(c, N_PASS, t);
  else {
    int lhs = expression(c, 0), decl = 0, element = 0, count = 0, op, final = 0;
    if (accept(c, ':')) {
      int annotation = annotation_name(c), module;
      char name[192];
      resolve_name(c, annotation, &module, name, sizeof(name));
      if (module < 0 && !strcmp(name, "typing.Final")) {
        final = 1;
        if (c->scope || c->depth)
          fail(c, t, "Final constants must be declared at module scope");
        if (accept(c, '[')) {
          decl = primitive(c);
          expect(c, ']', "expected ']' after Final type");
          if (decl == TY_ARRAY || decl == TY_VOID)
            fail(c, t, "constant type must be an integer, bool or str");
        }
      } else {
        decl = library_type(c, annotation);
        if (!decl)
          decl = TY_NAMED_BASE + annotation;
      }
      if (decl == TY_ARRAY) {
        expect(c, '[', "expected array[T, N]");
        element = primitive(c);
        expect(c, ',', "expected ','");
        count = expression(c, 0);
        expect(c, ']', "expected ']'");
      } else if (decl == TY_LIST) {
        expect(c, '[', "expected list[T]");
        element = primitive(c);
        if (element == TY_ARRAY || element == TY_LIST || element == TY_VOID)
          fail(c, t, "list elements must be integers, bool or str");
        expect(c, ']', "expected ']' after list element type");
      }
    }
    op = kind(c);
    if (op == '=' || op == T_ADDSET || op == T_SUBSET || op == T_MULSET ||
        op == T_ANDSET || op == T_ORSET || op == T_XORSET || op == T_POWERSET ||
        op == T_DIVSET || op == T_FLOORSET || op == T_MODSET || decl || final) {
      if (final && (c->nodes[lhs].kind != N_NAME || op != '='))
        fail(c, t, "Final declaration requires a value assigned to a name");
      n = node_new(c, final ? N_DECLCONST : N_ASSIGN, t);
      c->nodes[n].a = lhs;
      c->nodes[n].type = decl;
      c->nodes[n].c = element;
      c->nodes[n].d = count;
      if (accept(c, '='))
        c->nodes[n].b = expression(c, 0);
      else if ((op >= T_ADDSET && op <= T_XORSET) || op == T_POWERSET ||
               op == T_DIVSET || op == T_FLOORSET || op == T_MODSET) {
        int bin;
        ++c->cursor;
        if (decl)
          fail(c, t, "augmented assignment cannot declare a type");
        bin = node_new(c, N_BINARY, t);
        c->nodes[bin].a = lhs;
        c->nodes[bin].op = op == T_POWERSET   ? T_POWER
                           : op == T_DIVSET   ? '/'
                           : op == T_FLOORSET ? T_FLOORDIV
                           : op == T_MODSET   ? '%'
                           : op == T_ADDSET   ? '+'
                           : op == T_SUBSET   ? '-'
                           : op == T_MULSET   ? '*'
                           : op == T_ANDSET   ? '&'
                           : op == T_ORSET    ? '|'
                                              : '^';
        c->nodes[bin].b = expression(c, 0);
        c->nodes[n].b = bin;
        c->nodes[n].op = 1;
      } else if (!decl)
        fail(c, t, "expected assignment");
    } else {
      n = node_new(c, N_EXPR, t);
      c->nodes[n].a = lhs;
    }
  }
  expect(c, T_NEWLINE,
         "expected end of statement (feature may be unsupported)");
  return n;
}
void parse(Compiler *c) {
  int last = 0;
  while (kind(c) != T_EOF) {
    int n = statement(c);
    if (last)
      c->nodes[last].next = n;
    else
      c->root = n;
    last = n;
  }
}

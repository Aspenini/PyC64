#include "pyc64.h"
#include <ctype.h>
#include <limits.h>

static void emit(Compiler *c, int kind, int start, int len, int line, int col,
                 long value) {
  Token *t;
  if (c->ntokens == MAX_TOKENS)
    fail(c, c->ntokens - 1, "token arena exhausted (limit %d)", MAX_TOKENS);
  t = &c->tokens[c->ntokens++];
  t->kind = kind;
  t->start = start;
  t->length = len;
  t->line = line;
  t->column = col;
  t->value = value;
  t->module = c->active_module;
}
void token_text(Compiler *c, int token, char *out, size_t size) {
  Token *t = &c->tokens[token];
  size_t n = (size_t)t->length;
  if (n >= size)
    fail(c, token, "identifier is too long (limit %u)", (unsigned)(size - 1));
  memcpy(out, c->source + t->start, n);
  out[n] = 0;
}
void lex(Compiler *c) {
  Module *module = &c->modules[c->active_module];
  int p = module->start, end = p + module->length;
  int line = 1, col = 1, bol = 1, levels[64] = {0}, level = 0, brackets = 0,
      content = 0;
  const char *s = c->source;
  while (p < end) {
    int start, k;
    char ch;
    if (bol && !brackets) {
      int spaces = 0;
      while (s[p] == ' ') {
        ++spaces;
        ++p;
        ++col;
      }
      if (s[p] == '\t') {
        emit(c, T_EOF, p, 1, line, col, 0);
        fail(c, c->ntokens - 1, "tabs are not supported; indent with spaces");
      }
      if (s[p] != '#' && s[p] != '\r' && s[p] != '\n' && s[p]) {
        if (spaces > levels[level]) {
          if (level == 63)
            fail(c, c->ntokens - 1, "indentation nesting is too deep");
          levels[++level] = spaces;
          emit(c, T_INDENT, p, 0, line, col, 0);
        } else {
          while (spaces < levels[level]) {
            --level;
            emit(c, T_DEDENT, p, 0, line, col, 0);
          }
          if (spaces != levels[level])
            fail(c, c->ntokens - 1, "inconsistent indentation");
        }
      }
    }
    if (p >= end)
      break;
    bol = 0;
    ch = s[p];
    if (ch == ' ' || ch == '\r') {
      ++p;
      ++col;
      continue;
    }
    if (ch == '\t') {
      emit(c, T_EOF, p, 1, line, col, 0);
      fail(c, c->ntokens - 1, "tabs are not supported");
    }
    if (ch == '#') {
      while (s[p] && s[p] != '\n') {
        ++p;
        ++col;
      }
      continue;
    }
    if (ch == '\n') {
      if (content && !brackets) {
        emit(c, T_NEWLINE, p, 1, line, col, 0);
        content = 0;
      }
      ++p;
      ++line;
      col = 1;
      bol = 1;
      continue;
    }
    start = p;
    content = 1;
    if (isdigit((unsigned char)ch) ||
        (ch == '.' && isdigit((unsigned char)s[p + 1]))) {
      int q = p, real = ch == '.',
          prefixed = ch == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X' ||
                                   s[p + 1] == 'b' || s[p + 1] == 'B');
      if (!prefixed) {
        while (isdigit((unsigned char)s[q]) || s[q] == '_')
          ++q;
        if (s[q] == '.') {
          real = 1;
          ++q;
          while (isdigit((unsigned char)s[q]) || s[q] == '_')
            ++q;
        }
        if (s[q] == 'e' || s[q] == 'E') {
          real = 1;
          ++q;
          if (s[q] == '+' || s[q] == '-')
            ++q;
          while (isdigit((unsigned char)s[q]) || s[q] == '_')
            ++q;
        }
        if (real) {
          char number[256], *tail;
          int j, length = 0;
          emit(c, T_FLOAT, p, q - p, line, col, 0);
          for (j = p; j < q; ++j) {
            if (s[j] == '_') {
              if (j == p || j + 1 == q || !isdigit((unsigned char)s[j - 1]) ||
                  !isdigit((unsigned char)s[j + 1]))
                fail(c, c->ntokens - 1, "invalid underscore in float literal");
            } else {
              if (length == 255)
                fail(c, c->ntokens - 1, "float literal is too long");
              number[length++] = s[j];
            }
          }
          number[length] = 0;
          c->tokens[c->ntokens - 1].real = strtod(number, &tail);
          if (*tail || isalnum((unsigned char)s[q]))
            fail(c, c->ntokens - 1, "invalid float literal");
          col += q - p;
          p = q;
          continue;
        }
      }
    }
    {
      int prefix = 0, raw = 0;
      if (ch == 'b' || ch == 'B') {
        prefix = 1;
        if (s[p + 1] == 'r' || s[p + 1] == 'R')
          prefix = 2, raw = 1;
      } else if ((ch == 'r' || ch == 'R') &&
                 (s[p + 1] == 'b' || s[p + 1] == 'B'))
        prefix = 2, raw = 1;
      if (prefix && (s[p + prefix] == '\'' || s[p + prefix] == '"')) {
        char quote = s[p + prefix];
        p += prefix + 1;
        while (p < end && s[p] != quote && s[p] != '\n') {
          if (s[p] == '\\' && p + 1 < end)
            ++p;
          ++p;
        }
        if (p == end || s[p] != quote) {
          emit(c, T_BYTES, start, p - start, line, col, raw);
          fail(
              c, c->ntokens - 1,
              "unterminated bytes literal; multiline literals are unsupported");
        }
        ++p;
        emit(c, T_BYTES, start, p - start, line, col, raw);
        col += p - start;
        continue;
      }
    }
    if (isalpha((unsigned char)ch) || ch == '_') {
      do {
        ++p;
      } while (isalnum((unsigned char)s[p]) || s[p] == '_');
      emit(c, T_NAME, start, p - start, line, col, 0);
      col += p - start;
      continue;
    }
    if (isdigit((unsigned char)ch)) {
      long value = 0;
      int base = 10, digits = 0;
      if (ch == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X' || s[p + 1] == 'b' ||
                        s[p + 1] == 'B')) {
        base = (s[p + 1] == 'b' || s[p + 1] == 'B') ? 2 : 16;
        p += 2;
      }
      while (isalnum((unsigned char)s[p]) || s[p] == '_') {
        int digit;
        if (s[p] == '_') {
          ++p;
          continue;
        }
        digit = isdigit((unsigned char)s[p])
                    ? s[p] - '0'
                    : tolower((unsigned char)s[p]) - 'a' + 10;
        if (digit < 0 || digit >= base || value > (65535 - digit) / base) {
          emit(c, T_NUMBER, start, p - start + 1, line, col, 0);
          fail(c, c->ntokens - 1, "invalid integer or value exceeds 65535");
        }
        value = value * base + digit;
        ++digits;
        ++p;
      }
      if (!digits) {
        emit(c, T_NUMBER, start, p - start, line, col, 0);
        fail(c, c->ntokens - 1, "expected digits after integer prefix");
      }
      emit(c, T_NUMBER, start, p - start, line, col, value);
      col += p - start;
      continue;
    }
    if (ch == '\'' || ch == '"') {
      ++p;
      while (s[p] && s[p] != ch && s[p] != '\n') {
        if (s[p] == '\\' && s[p + 1])
          ++p;
        ++p;
      }
      if (s[p] != ch) {
        emit(c, T_STRING, start, p - start, line, col, 0);
        fail(c, c->ntokens - 1,
             "unterminated string (multiline strings are not supported)");
      }
      ++p;
      emit(c, T_STRING, start, p - start, line, col, 0);
      col += p - start;
      continue;
    }
    k = (unsigned char)ch;
    if (s[p + 1] == '=') {
      switch (ch) {
      case '=':
        k = T_EQ;
        break;
      case '!':
        k = T_NE;
        break;
      case '<':
        k = T_LE;
        break;
      case '>':
        k = T_GE;
        break;
      case '+':
        k = T_ADDSET;
        break;
      case '-':
        k = T_SUBSET;
        break;
      case '*':
        k = T_MULSET;
        break;
      case '/':
        k = T_DIVSET;
        break;
      case '%':
        k = T_MODSET;
        break;
      case '&':
        k = T_ANDSET;
        break;
      case '|':
        k = T_ORSET;
        break;
      case '^':
        k = T_XORSET;
        break;
      }
    }
    if (ch == '<' && s[p + 1] == '<')
      k = T_SHL;
    if (ch == '>' && s[p + 1] == '>')
      k = T_SHR;
    if (ch == '-' && s[p + 1] == '>')
      k = T_ARROW;
    if (ch == '/' && s[p + 1] == '/')
      k = s[p + 2] == '=' ? T_FLOORSET : T_FLOORDIV;
    if (ch == '*' && s[p + 1] == '*')
      k = s[p + 2] == '=' ? T_POWERSET : T_POWER;
    if (k >= 256)
      p += k == T_POWERSET || k == T_FLOORSET ? 3 : 2;
    else
      ++p;
    if (ch == '(' || ch == '[')
      ++brackets;
    if (ch == ')' || ch == ']') {
      if (!brackets) {
        emit(c, k, start, 1, line, col, 0);
        fail(c, c->ntokens - 1, "unmatched closing bracket");
      }
      --brackets;
    }
    if (!strchr("()[]:,.+-*/%&|^~<>=!@", ch)) {
      emit(c, k, start, 1, line, col, 0);
      fail(c, c->ntokens - 1, "unsupported character '%c'", ch);
    }
    emit(c, k, start, p - start, line, col, 0);
    col += p - start;
  }
  if (brackets)
    fail(c, c->ntokens - 1, "unclosed bracket");
  if (content)
    emit(c, T_NEWLINE, p, 0, line, col, 0);
  while (level--)
    emit(c, T_DEDENT, p, 0, line, col, 0);
  emit(c, T_EOF, p, 0, line, col, 0);
}

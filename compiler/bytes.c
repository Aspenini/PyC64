#include "pyc64.h"

/* Length-prefixed records allow every byte value, including embedded zeroes.
   Immutable literals are deduplicated independently of the ASCII string pool.
 */
void bytes_literal(Compiler *c, int node, const unsigned char *data,
                   int count) {
  Node *n = &c->nodes[node];
  int j, size = 0;
  if (count < 0 || count > MAX_BYTES - 2)
    fail(c, n->token, "bytes literal exceeds the %d-byte arena", MAX_BYTES);
  for (j = 0; j < c->nbytes; j += size + 2) {
    size = c->bytes[j] | (c->bytes[j + 1] << 8);
    if (size == count &&
        (!count || !memcmp(c->bytes + j + 2, data, (size_t)count)))
      break;
  }
  if (j == c->nbytes) {
    if (c->nbytes + count + 2 > MAX_BYTES)
      fail(c, n->token, "bytes literal arena exhausted (limit %d)", MAX_BYTES);
    c->bytes[j] = (unsigned char)count;
    c->bytes[j + 1] = (unsigned char)(count >> 8);
    if (count)
      memcpy(c->bytes + j + 2, data, (size_t)count);
    c->nbytes += count + 2;
  }
  n->kind = N_BYTES;
  n->type = TY_BYTES;
  n->value = j + 2;
  n->d = count;
  n->a = n->b = 0;
  n->symbol = -1;
}
void bytes_slice_literal(Compiler *c, int node) {
  Node *n = &c->nodes[node], *source = &c->nodes[n->a];
  unsigned char data[MAX_BYTES];
  long length = source->d, step = n->d ? c->nodes[n->d].value : 1;
  long start, stop, lower = step < 0 ? -1 : 0;
  long upper = step < 0 ? length - 1 : length, index;
  int count = 0;
  if (!step) {
    fail(c, n->token, "slice step cannot be zero");
    return;
  }
  start = n->b ? c->nodes[n->b].value : (step < 0 ? upper : 0);
  stop = n->c ? c->nodes[n->c].value : (step < 0 ? -1 : length);
  if (n->b && start < 0)
    start += length;
  if (n->c && stop < 0)
    stop += length;
  start = start < lower ? lower : start > upper ? upper : start;
  stop = stop < lower ? lower : stop > upper ? upper : stop;
  for (index = start; step < 0 ? index > stop : index < stop; index += step)
    data[count++] = c->bytes[source->value + index];
  bytes_literal(c, node, data, count);
}
int bytes_method(const char *name) {
  static const char *names[] = {"find",  "rfind",      "index",    "rindex",
                                "count", "startswith", "endswith", NULL};
  int j;
  for (j = 0; names[j]; ++j)
    if (!strcmp(name, names[j]))
      return j + 1;
  return 0;
}
int bytes_method_type(int method) {
  return method == BS_COUNT ? TY_U16 : method >= BS_PREFIX ? TY_BOOL : TY_I16;
}
void bytes_search_literal(Compiler *c, int node) {
  Node *n = &c->nodes[node], *source = &c->nodes[n->a],
       *needle = &c->nodes[n->b];
  int start_node = needle->next, end_node = c->nodes[start_node].next;
  long length = source->d,
       start = start_node && !(n->c & 2) ? c->nodes[start_node].value : 0;
  long end = end_node && !(n->c & 4) ? c->nodes[end_node].value : length;
  unsigned char byte = (unsigned char)needle->value;
  const unsigned char *data = c->bytes + source->value;
  const unsigned char *sub = n->c & 1 ? &byte : c->bytes + needle->value;
  long size = n->c & 1 ? 1 : needle->d, result = -1, index, last;
  int method = n->op;
  if (start < 0)
    start += length;
  if (end < 0)
    end += length;
  if (start < 0)
    start = 0;
  end = end < 0 ? 0 : end > length ? length : end;
  if (method >= BS_COUNT)
    result = 0;
  if (start <= length && start <= end && size <= end - start) {
    last = end - size;
    if (!size)
      result = method == BS_COUNT                          ? end - start + 1
               : method >= BS_PREFIX                       ? 1
               : method == BS_RFIND || method == BS_RINDEX ? end
                                                           : start;
    else if (method >= BS_PREFIX)
      result = !memcmp(data + (method == BS_PREFIX ? start : last), sub,
                       (size_t)size);
    else {
      for (index = start; index <= last; ++index)
        if (!memcmp(data + index, sub, (size_t)size)) {
          if (method == BS_COUNT) {
            ++result;
            index += size - 1;
          } else {
            result = index;
            if (method == BS_FIND || method == BS_INDEX)
              break;
          }
        }
    }
  }
  if (result < 0 && (method == BS_INDEX || method == BS_RINDEX))
    fail(c, n->token, "bytes subsequence was not found");
  n->kind = N_CONST;
  n->value = result;
  n->type = bytes_method_type(method);
  n->a = n->b = 0;
  n->c = 1;
}
static int hex(int ch) {
  return ch >= '0' && ch <= '9'   ? ch - '0'
         : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
         : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                  : -1;
}
int parse_bytes(Compiler *c, int token) {
  Token *t = &c->tokens[token];
  unsigned char data[MAX_BYTES];
  int p = t->start, end = p + t->length - 1, count = 0;
  int node = node_new(c, N_BYTES, token);
  while (c->source[p] != '\'' && c->source[p] != '"')
    ++p;
  ++p;
  while (p < end) {
    int ch = (unsigned char)c->source[p++];
    if (ch > 127 || ch == '\n')
      fail(c, token,
           "bytes literals require ASCII source characters; use escapes for "
           "128..255");
    if (ch == '\\' && !t->value) {
      ch = (unsigned char)c->source[p++];
      if (ch >= '0' && ch <= '7') {
        int digits = 1, value = ch - '0';
        while (digits++ < 3 && p < end && c->source[p] >= '0' &&
               c->source[p] <= '7')
          value = value * 8 + c->source[p++] - '0';
        if (value > 255)
          fail(c, token, "bytes octal escapes must fit 0..255");
        ch = value;
      } else if (ch == 'x') {
        if (p + 1 >= end || hex((unsigned char)c->source[p]) < 0 ||
            hex((unsigned char)c->source[p + 1]) < 0)
          fail(c, token, "bytes hex escape requires two hex digits");
        ch = hex((unsigned char)c->source[p]) * 16 +
             hex((unsigned char)c->source[p + 1]);
        p += 2;
      } else {
        switch (ch) {
        case 'a':
          ch = 7;
          break;
        case 'b':
          ch = 8;
          break;
        case 't':
          ch = 9;
          break;
        case 'n':
          ch = 10;
          break;
        case 'v':
          ch = 11;
          break;
        case 'f':
          ch = 12;
          break;
        case 'r':
          ch = 13;
          break;
        case '\\':
        case '\'':
        case '"':
          break;
        default:
          if (count >= MAX_BYTES) {
            fail(c, token, "bytes literal arena exhausted");
            return node;
          }
          data[count++] = '\\';
          if (ch > 127 || ch == '\n')
            fail(c, token, "unsupported byte escape");
        }
      }
    }
    if (count >= MAX_BYTES) {
      fail(c, token, "bytes literal arena exhausted");
      return node;
    }
    data[count++] = (unsigned char)ch;
  }
  bytes_literal(c, node, data, count);
  return node;
}

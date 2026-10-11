#include "pyc64.h"

int scalar_type(int t) {
  return (t >= TY_BOOL && t <= TY_STR) || t == TY_FLOAT || t == TY_BYTES ||
         enum_type(t);
}

/* A signed runtime exponent can be negative, so its result needs a float. */
int power_type(Compiler *c, int base, int exponent, int exponent_node) {
  Node *n = &c->nodes[exponent_node];
  if (!base || !exponent)
    return TY_NONE;
  if (base == TY_FLOAT || exponent == TY_FLOAT ||
      (n->kind == N_CONST ? n->value < 0 : type_signed(exponent)))
    return TY_FLOAT;
  return base == TY_BOOL ? TY_U8 : integer_type(base);
}

/* C64 packed float: biased exponent, sign and a 32-bit normalized significand.
   Work in host double without relying on its byte order or representation. */
void float_pack(Compiler *c, int token, double value, unsigned char *bytes) {
  int exponent = 129, sign = value < 0, j;
  uint64_t mantissa;
  memset(bytes, 0, 5);
  if (sign)
    value = -value;
  if (value == 0)
    return;
  if (!(value > 0) || value > 1.7014118342085515e38)
    fail(c, token, "float exceeds the finite C64 range");
  while (value >= 2) {
    value *= 0.5;
    ++exponent;
  }
  while (value < 1 && exponent > 0) {
    value *= 2;
    --exponent;
  }
  if (exponent <= 0)
    return;
  mantissa = (uint64_t)((value - 1) * 2147483648.0 + 0.5);
  if (mantissa == UINT64_C(2147483648)) {
    mantissa = 0;
    ++exponent;
  }
  if (exponent > 255)
    fail(c, token, "float exceeds the finite C64 range");
  bytes[0] = (unsigned char)exponent;
  for (j = 4; j > 0; --j) {
    bytes[j] = (unsigned char)mantissa;
    mantissa >>= 8;
  }
  bytes[1] |= sign ? 128 : 0;
}

/* Truncate the target-rounded value, then keep its low 16 bits. This matches
   the native packed-value conversion even at rounding and exponent boundaries.
 */
unsigned int float_integer(Compiler *c, int token, double value, int bounded) {
  unsigned char bytes[5];
  uint32_t significand;
  unsigned int word;
  int shift;
  float_pack(c, token, value, bytes);
  if (bounded && bytes[0] >= 144 &&
      (bytes[0] > 144 || !(bytes[1] & 128) || (bytes[1] & 127) || bytes[2]))
    fail(c, token, "int conversion result is outside the i16 range");
  if (bytes[0] < 129 || bytes[0] >= 176)
    return 0;
  significand = ((uint32_t)(bytes[1] | 128) << 24) |
                ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 8) |
                bytes[4];
  shift = (int)bytes[0] - 160;
  word =
      (unsigned int)(shift < 0 ? significand >> -shift : significand << shift) &
      65535;
  return bytes[1] & 128 ? (0u - word) & 65535 : word;
}

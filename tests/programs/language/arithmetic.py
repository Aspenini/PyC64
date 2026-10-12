# oracle: cpython
# Values stay in range, so CPython's unbounded ints agree.
from pyc64.types import u8, u16, i16

a: u16 = 50000
b: u16 = 1234
print(a + b // 2, a - b, a // b, a % b, a & b, a | b, a ^ b)
#> 50617 48766 40 640 80 51154 51074
c: u16 = 255
print(c * 257, c << 8, a >> 3, ~u8(5) & 255)
#> 65535 65280 6250 250
d: i16 = -1234
e: i16 = 56
print(d + e, d - e, d * 3, -d, d // e)
#> -1178 -1290 -3702 1234 -23
print(2 + 3 * 4 - 10 // 3, (2 + 3) * 4, 17 % 5 * 2, 1 << 3 + 1)
#> 11 20 4 16

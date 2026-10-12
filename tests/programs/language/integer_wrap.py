# Fixed-width integers wrap on overflow, unlike CPython ints.
from pyc64.types import u8, i8, u16, i16

a: u8 = 255
a += 1
b: i8 = -128
b -= 1
c: u16 = 65535
c = c + 1
d: i16 = -32768
d = d - 1
print(a, b, c, d)
#> 0 127 0 32767

big: u16 = 40000
product: u16 = big * 3
difference: u16 = u16(5) - u16(9)
print(product, difference)
#> 54464 65532

small: i8 = 100
total: i8 = small + small
print(total, i8(-128) // i8(-1))
#> -56 -128

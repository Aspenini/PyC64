# oracle: cpython
from pyc64.types import u8, i8, u16, i16

x = i8(128)
y = u16(-1)
z = bool(y)
t = True
total: u8 = t + t
negative: i8 = -t
print(x, y, z, total, negative)
#> -128 65535 TRUE 2 -1
print(u8(300), i16(65535), u16(i8(-2)), u8(i16(-1)))
#> 44 -1 65534 255

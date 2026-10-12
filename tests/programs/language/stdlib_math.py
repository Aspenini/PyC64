# oracle: cpython
from pyc64.math import min_u8, max_u16, clamp_u8

print(min_u8(42, 7), max_u16(65535, 32768), clamp_u8(0, 5, 10), clamp_u8(7, 5, 10), clamp_u8(255, 5, 10))
#> 7 65535 5 7 10

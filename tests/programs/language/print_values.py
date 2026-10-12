# oracle: cpython
from pyc64.types import u8, i8, u16, i16, char

a: i16 = -32768
b: i8 = -128
c: i16 = -1
d: u8 = 0
f: i8 = 127
g: u8 = 255
h: u16 = 256
i: i16 = 32767
j: u16 = 65535
print(a, b, c, d, f, g, h, i, j)
#> -32768 -128 -1 0 127 255 256 32767 65535
print(True, False, bool(j), char(65))
#> TRUE FALSE TRUE 65
print()
#>
print("", "score:", j, "")
#>  SCORE: 65535
print(0, 9, 10, 99, 100, 999, 1000, 9999, 10000, 60000)
#> 0 9 10 99 100 999 1000 9999 10000 60000
print(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)
#> 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
message = "hello\nworld"
print(message)
#> HELLO
#> WORLD

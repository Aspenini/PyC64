from pyc64.types import char, i16, i8, u16, u8
a: i16 = -32768
b: i8 = -128
c: i16 = -1
d: u8 = 0
e: u8 = 1
f: i8 = 127
g: u8 = 255
h: u16 = 256
i: i16 = 32767
j: u16 = 65535
print(a, b, c, d, e, f, g, h, i, j)
print(True, False, bool(j), char(65))
print()
print("", "score:", j, "")
#> -32768 -128 -1 0 1 127 255 256 32767 65535
#> TRUE FALSE TRUE 65
#>
#>  SCORE: 65535 

# oracle: cpython
# Floor division and modulo follow Python's sign rules.
from pyc64.types import i8, i16

def show(a: i16, b: i16):
    print(a // b, a % b)

show(-7, 3)
#> -3 2
show(7, -3)
#> -3 -2
show(-7, -3)
#> 2 -1
show(-6, 3)
#> -2 0
show(-32768, 1)
#> -32768 0
show(32767, -32768)
#> -1 -1
show(-1, 32767)
#> -1 32766
small: i8 = -100
divisor: i8 = 7
print(small // divisor, small % divisor, i8(-128) // i8(1), i8(127) % i8(-128))
#> -15 5 -128 -1
mixed: i16 = -100
wide = mixed // 255
print(wide, 255 % 7, (mixed // 7) // (20 // -3), (mixed % 7) % (20 % -3))
#> -1 3 2 0

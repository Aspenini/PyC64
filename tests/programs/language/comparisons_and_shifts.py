# oracle: cpython
from pyc64.types import i16, u16

def compare(a: i16, b: i16):
    print(a < b, a <= b, a > b, a >= b, a == b, a != b, a >> 3)

compare(-32768, 32767)
#> TRUE TRUE FALSE FALSE FALSE TRUE -4096
compare(-1, 0)
#> TRUE TRUE FALSE FALSE FALSE TRUE -1
compare(5, 5)
#> FALSE TRUE FALSE TRUE TRUE FALSE 0
compare(123, -9)
#> FALSE FALSE TRUE TRUE FALSE TRUE 15
compare(-200, -100)
#> TRUE TRUE FALSE FALSE FALSE TRUE -25
large: u16 = 65535
print(large > 32767, u16(1) << 15, large >> 15)
#> TRUE 32768 1

# oracle: cpython
from pyc64.types import i16, u16, u8
print(-2**2, (-2)**2, 2**3**2, 0**0, 0**3, 1**65535)
def constant():
    return pow(2, 8)
constant_value = constant()
pow(2, 8)
base: u16 = 3
exponent: u8 = 5
value = base ** exponent
base **= 2
negative: i16 = -3
odd = negative ** exponent
print(value, base, odd, pow(negative, 2), pow(2, 8))
#> -4 4 512 1 0 1
#> 243 9 -243 9 256

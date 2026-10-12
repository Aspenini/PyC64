# oracle: cpython
# and/or return an operand and short-circuit like Python.
from pyc64.types import u16, i16

def effect() -> u16:
    print("effect")
    return 1234

zero: u16 = 0
seven: u16 = 7
large: u16 = 513
a: u16 = zero and effect()
b: u16 = seven or effect()
c: u16 = seven and large
d: u16 = zero or large
print(a, b, c, d, 7 and 513, 7 or 513)
#> 0 7 513 513 513 7
negative: i16 = -7
print(negative or i16(8), not seven, not zero)
#> -7 FALSE TRUE
e: u16 = seven and effect()
print(e)
#> EFFECT
#> 1234

# oracle: cpython
from enum import IntEnum
class Bits(IntEnum):
    ZERO = 0
    LOW = 2
    HIGH = 8
left = Bits.LOW
right = Bits.HIGH
def add(a, b):
    return a + b
def folded():
    return Bits.HIGH ** 3
number = add(left, right)
power = folded()
print(number, left | right, left << 1, int(left), +left, -left, left == 2)
print(power, (left or right).name, bool(Bits.ZERO))
#> 10 10 4 2 2 -2 TRUE
#> 512 LOW FALSE

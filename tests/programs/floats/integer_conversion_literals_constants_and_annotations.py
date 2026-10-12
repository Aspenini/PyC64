# oracle: cpython
from pyc64.types import i16, i8, u16, u8
from typing import Final
ZERO: Final[int] = int()
LIMIT: Final[u16] = u16(65535.9)
NEGATIVE: Final[i16] = int(-12.75)
class Reading:
    value: int = int(-1.9)
reading = Reading()
positive: int = int(12.9)
negative = int(-12.9)
unchanged = int(65535)
flag = int(True)
print(ZERO, LIMIT, NEGATIVE, reading.value, positive, negative, unchanged, flag)
print(u8(257.9), i8(255.9), u16(-1.9), i16(65535.9), int(-32768.5))
#> 0 65535 -12 -1 12 -12 65535 1
#> 1 -1 65535 -1 -32768

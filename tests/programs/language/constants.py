# oracle: cpython
from typing import Final
from pyc64.types import u8, i8, i16, u16, array

START: Final[i16] = -3
STOP: Final = 4
COUNT: Final[u8] = STOP - START
ENABLED: Final[bool] = COUNT == 7
values: array[i16, COUNT] = array(i16, COUNT)
total: i16 = 0
for i in range(START, STOP):
    values[i - START] = i
    total += i
print(total, len(values), values[COUNT - 1], ENABLED)
#> 0 7 3 TRUE
MESSAGE: Final[str] = "constants work"
COPY: Final = MESSAGE
SIZE: Final = len(COPY)
SEVEN: Final = 7
THREE: Final = 3
print(COPY, SIZE, -SEVEN // THREE, -SEVEN % THREE, -SEVEN >> 1, bool(123))
#> CONSTANTS WORK 14 -3 2 -4 TRUE
LARGE: Final = 255
print(u8(LARGE + 1), i8(LARGE), ~u8(0) & 255)
#> 0 -1 255

from pyc64.types import array, u16, u8
from typing import Final
from c64.video import screen

COUNT: Final[u8] = 10
FACTOR: Final[u16] = 10
EXPECTED: Final[u16] = COUNT * (COUNT - 1) // 2 * FACTOR
MESSAGE: Final[str] = "CONSTANTS WORK"

values: array[u16, COUNT] = array(u16, COUNT)
total: u16 = 0

for i in range(COUNT):
    values[i] = u16(i) * FACTOR
    total += values[i]

screen.clear()
if total == EXPECTED:
    screen.border = 5
    print(MESSAGE)
else:
    screen.border = 2
    print("UNEXPECTED RESULT")

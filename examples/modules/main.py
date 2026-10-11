from pyc64.types import u16, u8
from c64.video import screen
from pyc64.math import clamp_u8
import scoring as game

total: u16 = 0
for i in range(10):
    total = total + game.points(u8(i))

screen.clear()
screen.border = clamp_u8(5, 0, 15)
if total == 450:
    print("MODULES WORK")
else:
    print("UNEXPECTED RESULT")

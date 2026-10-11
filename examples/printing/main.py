from pyc64.types import u16, i16
from c64.video import screen

screen.clear()
score: u16 = 450
minimum: i16 = -32768
maximum: u16 = 65535
print("Score:", score)
print("Limits:", minimum, maximum)
print("Ready:", True, "Paused:", False)
print()

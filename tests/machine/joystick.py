# Joystick 2 up + fire (mask 17) clears bits 0 and 4 of CIA 1 port A.
# frames: 2
# joystick: 17
# expect: 0xC000=14
from c64.memory import poke, peek
poke(0xDC02, 0)
poke(0xC000, peek(0xDC00) & 31)
while True:
    pass

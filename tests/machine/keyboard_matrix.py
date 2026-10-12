# Holding A pulls row 1, column 2 of the CIA keyboard matrix low.
# frames: 2
# key: 65
# expect: 0xC000=251
from c64.memory import poke, peek
poke(0xDC02, 255)
poke(0xDC03, 0)
poke(0xDC00, 253)
poke(0xC000, peek(0xDC01))
while True:
    pass

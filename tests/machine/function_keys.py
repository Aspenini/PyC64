# F1 sits in row 0, column 4 of the keyboard matrix.
# frames: 2
# key: 241
# expect: 0xC000=239
from c64.memory import poke, peek
poke(0xDC02, 255)
poke(0xDC03, 0)
poke(0xDC00, 254)
poke(0xC000, peek(0xDC01))
while True:
    pass

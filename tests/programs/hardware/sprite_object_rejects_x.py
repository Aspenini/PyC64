# Invalid values trap before any register changes.
# fails: BRK guard
from c64.sprite import Sprite, shared_colors
from c64.memory import poke
from pyc64.types import u16
for offset in range(48):
    poke(0xD000 + u16(offset), 0x55)
p = Sprite(0)
p.x = 512
# expect: 0xD000=0x55
# expect: 0xD027=0x55
# expect: 0xD025=0x55

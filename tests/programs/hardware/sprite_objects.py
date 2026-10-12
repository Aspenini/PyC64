from c64.sprite import Sprite, shared_colors
from c64.memory import poke
from pyc64.types import u16
for offset in range(48):
    poke(0xD000 + u16(offset), 0x55)
p = Sprite(2)
p.x = 511
p.y = 255
p.color = 15
p.set_data(16320)
p.enabled = True
p.multicolor = True
p.expand_x = True
p.expand_y = True
shared_colors(2, 5)
print(p.index, p.x, p.y, p.color, p.enabled, p.multicolor, p.expand_x, p.expand_y)
p.x = 0
p.y = 0
p.disable()
p.multicolor = False
p.expand_x = False
p.expand_y = False
# expect: 0xD010=0x51
# expect: 0xD015=0x51
# expect: 0xD01C=0x51
# expect: 0xD01D=0x51
# expect: 0xD017=0x51
# expect: 0xD004=0
# expect: 0xD005=0
# expect: 0xD029=15
# expect: 0x07FA=255
# expect: 0xD025=2
# expect: 0xD026=5
# expect: 0xD000=0x55
# expect: 0xD028=0x55
#> 2 511 255 15 TRUE TRUE TRUE TRUE

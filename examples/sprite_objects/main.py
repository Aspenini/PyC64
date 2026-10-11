from c64.video import screen
from c64.memory import poke, peek
from c64.sprite import Sprite, shared_colors
from pyc64.types import u16

screen.clear()

# Caller-managed bitmap in VIC bank 0, outside this small program's storage.
for offset in range(64):
    poke(0x3000 + u16(offset), 255)

player = Sprite(0)
player.set_data(0x3000)
player.x = 300
player.y = 80
player.color = 1
player.multicolor = True
player.expand_x = True
player.expand_y = False
shared_colors(2, 5)
player.enable()
player.x -= 1

print("Sprite:", player.x, player.y, player.color, player.enabled)
print("Modes:", player.multicolor, player.expand_x, player.expand_y)
x = player.x
y = player.y
color = player.color
pointer = peek(0x07F8)

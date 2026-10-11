from c64.assets import bitmap, bitmap_screen, address
from c64 import bitmap as display
from c64.memory import poke, peek
from pyc64.types import u16

pixels = bitmap("landscape.png")
attributes = bitmap_screen("landscape.png")
poke(0xCC, 1)
poke(0xD020, 0)
shown = display.show(pixels, attributes)
location = address(pixels)
mode = peek(0xD011) & 0x60
memory = peek(0xD018)
pixel_sum: u16 = 0
attribute_sum: u16 = 0
for value in pixels:
    pixel_sum += value
for value in attributes:
    attribute_sum += value

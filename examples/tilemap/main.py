from pyc64.types import u8, u16
from c64.assets import tileset, tilemap, address
from c64 import tiles
from c64.video import screen
from c64.memory import peek, poke

glyphs = tileset("level.png")
level = tilemap("level.png")
screen.clear()
screen.border = 6
screen.background = 0
# The demo leaves a graphical screen behind; disable the KERNAL cursor blink.
poke(0x00CC, 1)

columns: u16 = tiles.width(level)
rows: u16 = tiles.height(level)
drawn: bool = tiles.draw_region(level, glyphs, 4, 3, 40, 25, 0, 0, 13)
font_location: u16 = address(glyphs)
font_selected: bool = (u16(peek(0xD018) & 14) * 1024) == font_location
assert columns == 48 and rows == 30 and drawn and font_selected

screen_sum: u16 = 0
color_sum: u16 = 0
for offset in range(1000):
    screen_sum += u16(peek(0x0400 + u16(offset)))
    color_sum += u16(peek(0xD800 + u16(offset)) & 15)
first_tile: u8 = peek(0x0400)
last_tile: u8 = peek(0x07E7)
assert color_sum == 13000

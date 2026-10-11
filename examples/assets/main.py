from pyc64.types import u8, u16
from pyc64.assets import load
from c64.assets import sprite, sprite_multicolor, charset, address
from c64.sprite import Sprite, shared_colors
from c64.video import screen
from c64.memory import peek, poke

frames = sprite("ships.png")
colored = sprite_multicolor("color.png", "000000,ff0000,00ff00,0000ff")
font = charset("font.png")
level = load("level.bin")

screen.clear()
screen.border = 6
screen.background = 0

hero_address: u16 = address(frames)
hero = Sprite(0)
hero.set_data(hero_address)
hero.x = 100
hero.y = 100
hero.color = 1
hero.enable()

second = Sprite(1)
second.set_data(hero_address + 64)
second.x = 180
second.y = 100
second.color = 7
second.enable()

third = Sprite(2)
third.set_data(address(colored))
third.x = 260
third.y = 100
third.color = 5
third.multicolor = True
shared_colors(2, 6)
third.enable()

font_address: u16 = address(font)
poke(0xD018, (peek(0xD018) & 0xF1) | u8(font_address // 1024))
font_selected: bool = (u16(peek(0xD018) & 14) * 1024) == font_address
pointer_ok: bool = u16(peek(0x07F8)) * 64 == hero_address
frame_size: u16 = len(frames)
font_size: u16 = len(font)
checksum: u16 = 0
for value in level:
    checksum += value
assert frame_size == 128 and font_size == 2048
assert pointer_ok and font_selected and checksum == 32640
print("Assets:", frame_size, font_size)
print("Level:", checksum)

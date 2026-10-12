import c64.sprite as sprite
from c64.memory import poke
from pyc64.types import u8, u16
for offset in range(16):
    poke(0xD000 + u16(offset), 34)
poke(0xD015, 0x55)
poke(0xD010, 0x55)
slot: u8 = 3
x: u16 = 511
y: u8 = 255
sprite.enable(slot)
sprite.position(slot, x, y)
sprite.color(slot, 15)
sprite.data(slot, 16320)
# expect: 0xD015=0x5D
# expect: 0xD010=0x5D
# expect: 0xD006=255
# expect: 0xD007=255
# expect: 0xD02A=15
# expect: 0x07FB=255
# expect: 0xD004=34
# expect: 0xD008=34

from typing import Final
from pyc64.types import u16
from c64.memory import peek, poke
from c64.video import screen
import c64.sprite as sprite

# Caller-owned, 64-byte-aligned data outside this demo's PRG.
BITMAP: Final[u16] = 12288
for offset in range(63):
    poke(BITMAP + u16(offset), 255)

screen.clear()
sprite.data(0, BITMAP)
sprite.color(0, 5)
sprite.position(0, 320, 100)
sprite.enable(0)
enabled = peek(53269)
x_high = peek(53264)
data_block = peek(2040)
print("Sprite ready")

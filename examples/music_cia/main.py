from c64 import music, irq
from c64.memory import peek
from pyc64.types import array, u16

rasters = array(u16, 1)


def callback():
    rasters[0] = rasters[0] + 1


tune = music.load("theme.sid")
slow = music.load("slow.sid")
owned = irq.on_raster(100, callback)
before = (u16(peek(0xA1)) << 8) | u16(peek(0xA2))
started = music.play(tune)
assert started
while music.frames() < 48:
    pass
played = music.frames()
normal_rasters = rasters[0]
normal_jiffies = ((u16(peek(0xA1)) << 8) | u16(peek(0xA2))) - before
stopped = music.stop()
assert stopped
assert normal_jiffies >= 45 and normal_jiffies <= 52
if peek(0x02A6):
    assert normal_rasters >= 35 and normal_rasters <= 45
else:
    assert normal_rasters >= 44 and normal_rasters <= 52

rasters[0] = 0
before = (u16(peek(0xA1)) << 8) | u16(peek(0xA2))
slow_started = music.play(slow)
assert slow_started
while music.frames() < 48:
    pass
slow_played = music.frames()
slow_rasters = rasters[0]
slow_jiffies = ((u16(peek(0xA1)) << 8) | u16(peek(0xA2))) - before
slow_stopped = music.stop()
assert slow_stopped
assert slow_rasters >= 55
assert slow_jiffies >= 65
removed = irq.off()
after_stop = music.playing()
restored_a = peek(0xDC0E)
restored_b = peek(0xDC0F)
assert restored_a == 1
assert restored_b == 8
print("CIA calls:", played, slow_played)

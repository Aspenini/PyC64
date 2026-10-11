from pyc64.types import u8, u16, array
from c64 import irq
from c64.video import screen
from c64.memory import peek

ticks = array(u8, 1)


def on_frame() -> None:
    ticks[0] = ticks[0] + 1
    screen.border = ticks[0]
    if ticks[0] == 5:
        irq.off()


screen.clear()
saved_vector: u16 = u16(peek(0x0314)) | (u16(peek(0x0315)) << 8)
saved_mask: u8 = peek(0xD01A) & 15
assert irq.on_raster(100, on_frame)
total: float = 0.0
for index in range(100):
    total += 1.25 * 1.5 - 0.25
while irq.active():
    pass
count: u8 = ticks[0]
assert count == 5 and total == 162.5
restored_vector: u16 = u16(peek(0x0314)) | (u16(peek(0x0315)) << 8)
restored_mask: u8 = peek(0xD01A) & 15
assert restored_vector == saved_vector and restored_mask == saved_mask
print("IRQ float:", count, total)

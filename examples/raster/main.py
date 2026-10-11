from pyc64.types import u8, u16, array
from c64 import irq
from c64.video import screen
from c64.memory import peek

ticks = array(u8, 1)
results = array(u16, 1)


def shared_work(value: u16) -> u16:
    local = array(u16, 3)
    local[0] = value
    local[1] = value * 3
    local[2] = 17
    total: u16 = 0
    for index in range(3):
        total += local[index]
    return total


def on_frame() -> None:
    ticks[0] = ticks[0] + 1
    results[0] = shared_work(u16(ticks[0]))
    screen.border = ticks[0]
    if ticks[0] == 8:
        irq.off()
    else:
        irq.set_line(100)


screen.clear()
saved_vector: u16 = u16(peek(0x0314)) | (u16(peek(0x0315)) << 8)
saved_mask: u8 = peek(0xD01A) & 15
installed: bool = irq.on_raster(100, on_frame)
assert installed
iterations: u16 = 0
while irq.active():
    value: u16 = shared_work(1234)
    assert value == 4953
    iterations += 1
count: u8 = ticks[0]
callback_result: u16 = results[0]
assert count == 8 and callback_result == 49
restored_vector: u16 = u16(peek(0x0314)) | (u16(peek(0x0315)) << 8)
restored_mask: u8 = peek(0xD01A) & 15
assert restored_vector == saved_vector and restored_mask == saved_mask
print("Raster:", count, callback_result)

# Hires bitmap display in VIC bank 0: pixels $2000, attributes $0400.
# Callers keep bank 0 selected and own screen contents after text().
from pyc64.types import u16
from c64.assets import address
from c64.memory import peek, poke
from c64 import irq
from c64._video import mode


def show(pixels: bytes, attributes: bytes) -> bool:
    if len(pixels) != 8000 or len(attributes) != 1000:
        return False
    if address(pixels) != 8192:
        return False
    saved: bool = irq.disable()
    if (peek(0xD01A) & 1) != 0 and not irq.active():
        irq.restore(saved)
        return False
    for i in range(1000):
        poke(0x0400 + u16(i), attributes[i])
    mode(1)
    irq.restore(saved)
    return True


def text() -> bool:
    saved: bool = irq.disable()
    if (peek(0xD01A) & 1) != 0 and not irq.active():
        irq.restore(saved)
        return False
    mode(0)
    irq.restore(saved)
    return True

from pyc64.types import u8, u16
from c64 import reu
from c64.memory import peek, poke
from c64.video import screen

# Run with a 16 MiB REU. This demo writes the first byte of every bank and
# exercises the maximum count near the end of its 24-bit address space.
screen.clear()
available: bool = reu.present()
checksum: u16 = 0
filled: bool = False
end_value: u8 = 0
last_value: u8 = 0
wrap_rejected: bool = False
if available:
    for index in range(256):
        bank: u8 = u8(index)
        poke(0xC000, bank ^ 165)
        assert reu.store(0xC000, 0, bank, 1)
    for index in range(256):
        bank = u8(index)
        assert reu.fetch(0xC001, 0, bank, 1)
        value: u8 = peek(0xC001)
        assert value == (bank ^ 165), "16 MiB REU required"
        checksum += u16(value)
    poke(0xC000, 204)
    filled = reu.fill(0xC000, 65535, 254, 65535)
    assert reu.fetch(0xC001, 65533, 255, 1)
    end_value = peek(0xC001)
    poke(0xC000, 90)
    assert reu.store(0xC000, 65535, 255, 1)
    assert reu.fetch(0xC001, 65535, 255, 1)
    last_value = peek(0xC001)
    wrap_rejected = not reu.store(0xC000, 65535, 255, 2)
    assert checksum == 32640 and filled and end_value == 204 and last_value == 90 and wrap_rejected
print("REU:", available, "Banks:", checksum)
print("Maximum:", filled, end_value)
print("Last:", last_value, "Wrap:", wrap_rejected)

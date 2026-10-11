from pyc64.types import u8, u16
from c64 import reu
from c64.memory import peek, poke
from c64.video import screen

screen.clear()
available: bool = reu.present()
assert available, "REU required"

# Caller-owned scratch RAM. Start near a 64 KiB REU bank boundary.
payload = b"\x00\xff\x2a\x80\xfe\x01"
for index in range(len(payload)):
    poke(0xC000 + u16(index), payload[index])
stored: bool = reu.store(0xC000, 65532, 2, len(payload))
fetched: bool = reu.fetch(0xC100, 65532, 2, len(payload))
verified: bool = reu.verify(0xC100, 65532, 2, len(payload))
poke(0xC100, 99)
mismatch: bool = reu.verify(0xC100, 65532, 2, len(payload))

for index in range(6):
    poke(0xC200 + u16(index), u8(index + 1))
swapped: bool = reu.swap(0xC200, 65532, 2, 6)
assert reu.fetch(0xC300, 65532, 2, 6)
checksum: u16 = 0
replacement: u16 = 0
for index in range(6):
    checksum += u16(peek(0xC200 + u16(index)))
    replacement += u16(peek(0xC300 + u16(index)))

poke(0xC400, 85)
filled: bool = reu.fill(0xC400, 256, 4, 16)
assert reu.fetch(0xC500, 256, 4, 16)
fill_sum: u16 = 0
for index in range(16):
    fill_sum += u16(peek(0xC500 + u16(index)))
repeated: bool = reu.transfer(0xC600, 65532, 2, 6, reu.FROM_REU, reu.FIX_REU)
repeat_sum: u16 = 0
for index in range(6):
    repeat_sum += u16(peek(0xC600 + u16(index)))

assert stored and fetched and verified and not mismatch
assert swapped and filled and repeated
assert checksum == 680 and replacement == 21
assert fill_sum == 1360 and repeat_sum == 6
print("REU:", stored, fetched, verified, mismatch)
print("Swap:", checksum, replacement)
print("Fill:", fill_sum, "Repeat:", repeat_sum)

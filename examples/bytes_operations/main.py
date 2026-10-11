from pyc64.types import u16
from c64.video import screen


def checksum(data: bytes) -> u16:
    total: u16 = 0
    for value in data:
        total += value
    return total


screen.clear()
prefix = b"\x00\xff"
body = bytes([42, 65])
packet = prefix + body
saved = packet
count: u16 = 3
repeated = count * packet
packet += b"\x80"
packet *= 2

joined_length: u16 = len(saved)
joined_checksum: u16 = checksum(saved)
repeated_length: u16 = len(repeated)
repeated_checksum: u16 = checksum(repeated)
updated_length: u16 = len(packet)
updated_checksum: u16 = checksum(packet)
print("Joined:", joined_length, joined_checksum)
print("Repeated:", repeated_length, repeated_checksum)
print("Updated:", updated_length, updated_checksum)
print("Saved:", saved)
assert saved == prefix + body
assert len(repeated) == 12 and repeated[-1] == 65
assert len(packet) == 10 and packet[-1] == 128

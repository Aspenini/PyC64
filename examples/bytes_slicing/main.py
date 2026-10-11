from pyc64.types import i16, u16
from c64.video import screen


def checksum(data: bytes) -> u16:
    total: u16 = 0
    for value in data:
        total += value
    return total


screen.clear()
data = b"\x00\xff\x2aA\x80\x00\x07"
start: i16 = 1
stop: i16 = 6
step: i16 = 2
forward = data[start:stop:step]
reverse = data[::-1]
saved = data[1:5]
data = b"changed"

forward_length: u16 = len(forward)
forward_checksum: u16 = checksum(forward)
reverse_length: u16 = len(reverse)
reverse_checksum: u16 = checksum(reverse)
print("Forward:", forward_length, forward_checksum)
print("Reverse:", reverse_length, reverse_checksum)
print("Saved:", saved)
print("Every second:", reverse[::2])
assert saved == b"\xff\x2aA\x80"
assert reverse[0] == 7 and reverse[-1] == 0
assert saved[-1::-2] == b"\x80\x2a"

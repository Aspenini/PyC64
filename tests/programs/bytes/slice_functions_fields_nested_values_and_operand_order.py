# oracle: cpython
from pyc64.types import i16, u16
class Packet:
    _data: bytes = b"\x00\xffABC"
    calls: u16 = 0
    @property
    def data(self):
        self.calls = u16(self.calls * 10 + 1)
        return self._data
def bound(packet: Packet, value: i16, tag: u16) -> i16:
    packet.calls = u16(packet.calls * 10 + tag)
    packet._data = b"changed"
    return value
def cut(data, start: i16, stop: i16, step: i16):
    return data[start:stop:step]
packet = Packet()
saved = packet.data[bound(packet, -1, 2):bound(packet, -6, 3):bound(packet, -2, 4)]
print(saved, packet._data, packet.calls)
other = cut(saved, 0, 3, 2)
print(other, saved, cut(b"ABCDEFG", 1, 6, 2)[::-1])
items = [saved, other]
print(items[0][1:], (saved + other)[::2], (2 * saved)[::-2])
#> B'CA\X00' B'CHANGED' 1234
#> B'C\X00' B'CA\X00' B'FDB'
#> B'A\X00' B'C\X00\X00' B'\X00CA'

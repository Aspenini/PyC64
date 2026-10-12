# oracle: cpython
from pyc64.types import u16
class Packet:
    _data: bytes = b"\x00"
    calls: u16 = 0
    @property
    def data(self):
        self.calls = u16(self.calls * 10 + 1)
        return self._data
    @data.setter
    def data(self, value):
        self.calls = u16(self.calls * 10 + 3)
        self._data = value
def suffix(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 2)
    packet._data = b"changed"
    return b"\xff"
packet = Packet()
saved = packet._data
packet.data += suffix(packet)
print(saved, packet._data, packet.calls)
packet.calls = 0
packet.data *= 3
print(packet._data, packet.calls)
packet._data += b"A"
value = packet._data
old = value
value *= 2
value += b"end"
print(value, old, saved)
#> B'\X00' B'\X00\XFF' 123
#> B'\X00\XFF\X00\XFF\X00\XFF' 13
#> B'\X00\XFF\X00\XFF\X00\XFFA\X00\XFF\X00\XFF\X00\XFFAEND' B'\X00\XFF\X00\XFF\X00\XFFA' B'\X00'

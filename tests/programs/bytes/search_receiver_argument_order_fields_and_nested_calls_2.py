# oracle: cpython
from pyc64.types import u8
class Packet:
    _data: bytes = b"ABAB"
    reads: u8 = 0
    @property
    def data(self):
        self.reads += 1
        return self._data
def change(packet: Packet):
    packet._data = b"changed"
    return b"AB"
packet = Packet()
print(packet.data.rfind(change(packet)), packet.reads, packet._data)
#> 2 1 B'CHANGED'

# oracle: cpython
from pyc64.types import u8
class Packet:
    _data: bytes = b"saved"
    reads: u8 = 0
    @property
    def data(self):
        self.reads += 1
        return self._data
def mutate(packet: Packet):
    packet._data = b"different"
    return b"saved-different"
packet = Packet()
print(packet.data in mutate(packet), packet.reads, packet._data)
print((b"AB" + b"CD") in b"--ABCD--", b"BC" in b"ABCD"[1:3])
data = b"ABCAB"
needle = b"AB"
print(needle in bytes(data), b"AB" in data)
#> TRUE 1 B'DIFFERENT'
#> TRUE TRUE
#> TRUE TRUE

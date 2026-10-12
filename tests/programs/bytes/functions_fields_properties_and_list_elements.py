# oracle: cpython
class Packet:
    _data: bytes = b"\x00\xff"
    @property
    def data(self) -> bytes:
        return self._data
    @data.setter
    def data(self, value: bytes):
        self._data = value
def identity(value):
    return value
def choose(flag):
    if flag:
        return b"yes"
    return b"no"
def update(values: list[bytes]):
    values[1] = choose(True)
packet = Packet()
items = [b"first", packet.data]
update(items)
saved = identity(packet.data)
packet.data = items[1]
print(saved, packet.data, items[0], items[1], packet.data[-1])
#> B'\X00\XFF' B'YES' B'FIRST' B'YES' 115

# oracle: cpython
from pyc64.types import i16, u16
class Packet:
    data: bytes = b"AB\x00AB\xff"
    calls: u16 = 0
def receiver(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 1)
    return packet.data
def needle(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 2)
    packet.data = b"changed"
    return b"AB"
def start(packet: Packet) -> i16:
    packet.calls = u16(packet.calls * 10 + 3)
    return 1
def end(packet: Packet) -> u16:
    packet.calls = u16(packet.calls * 10 + 4)
    return 6
packet = Packet()
found = receiver(packet).find(needle(packet), start(packet), end(packet))
print(found, packet.calls, packet.data)
def search(data, needle):
    return data.rfind(needle)
print(search(b"ABCAB", b"AB"), search(b"ABCA", b"B"))
print((b"AB" + b"AB").count(b"AB"), b"ABC"[::-1].find(b"B"))
from enum import IntEnum
class Bound(IntEnum):
    START = 0
    END = 5
    BYTE = 65
data = b"ABCAB"
print(data[Bound.START:Bound.END].find(Bound.BYTE, Bound.START, Bound.END))
#> 3 1234 B'CHANGED'
#> 3 1
#> 2 1
#> 0

# oracle: cpython
from pyc64.types import u16
class Packet:
    data: bytes = b"old"
    calls: u16 = 0
def needle(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 1)
    packet.data = b"old"
    return packet.data
def container(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 2)
    packet.data = b"new"
    assert b"new" in packet.data
    return b"old-new"
def contains(needle, data):
    return needle in data
def contains_byte(needle, data):
    return needle not in data
packet = Packet()
print(needle(packet) in container(packet), packet.calls, packet.data)
print(contains(b"old", b"old-new"), contains(b"\xff", b"\xff"))
print(contains(b"absent", b"old-new"))
print(contains_byte(255, b"\xff"), contains_byte(254, b"\xff"))
#> TRUE 12 B'NEW'
#> TRUE TRUE
#> FALSE
#> FALSE TRUE

# oracle: cpython
from pyc64.types import array, i16, u8
from enum import IntEnum
class Value(IntEnum):
    ZERO = 0
    HIGH = 255
values = array(Value, 3)
values[1] = Value.HIGH
def capture(values: array[Value]):
    return bytes(values)
saved = capture(values)
values[1] = Value.ZERO
print(saved, capture(values))
counter = array(u8, 1)
def next_byte():
    counter[0] = u8(counter[0] + 1)
    return counter[0]
result = bytes([next_byte(), next_byte(), next_byte()])
print(result, counter[0])
data = b"AB"
def change() -> i16:
    data = b"CD"
    return -1
print(data[change()])
class Packet:
    data: bytes = b"AB"
def replace(packet: Packet) -> i16:
    packet.data = b"CD"
    return -1
packet = Packet()
print(packet.data[replace(packet)], packet.data)
#> B'\X00\XFF\X00' B'\X00\X00\X00'
#> B'\X01\X02\X03' 3
#> 66
#> 66 B'CD'

# oracle: cpython
from typing import Final
PRESENT: Final[bool] = b"\x00\xff" in b"A\x00\xffB"
ABSENT: Final[bool] = 255 not in b"ABC"
class Packet:
    present: bool = b"" in b""
packet = Packet()
print(PRESENT, ABSENT, packet.present, b"long" not in b"A")
data = b"\x00\x01AB\xff"
print(1 + 64 in data, 128 | 127 in data, not 65 in data, not 66 not in data)
print(65 in data and 255 in data, 99 not in data or 99 in data)
print((65 in data) == True, (65 not in data) == False)
from enum import IntEnum
class Byte(IntEnum):
    A = 65
print(Byte.A in data, True in data, False in data)
#> TRUE TRUE TRUE TRUE
#> TRUE TRUE FALSE TRUE
#> TRUE TRUE
#> TRUE TRUE
#> TRUE TRUE TRUE

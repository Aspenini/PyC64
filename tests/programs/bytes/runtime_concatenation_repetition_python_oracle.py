# oracle: cpython
from pyc64.types import i8
from enum import IntEnum
class Count(IntEnum):
    NONE = -1
    TWO = 2
source = b"\x00\xff"
print(source * Count.TWO, Count.TWO * source, source * Count.NONE)
small: i8 = -128
print(source * small)
#> B'\X00\XFF\X00\XFF' B'\X00\XFF\X00\XFF' B''
#> B''

# oracle: cpython
from pyc64.types import i8
from enum import IntEnum
class Bound(IntEnum):
    START = -1
    STOP = -7
    STEP = -2
data = b"AB\x00\xffCD"
start: i8 = -1
stop: i8 = -128
step: i8 = -2
print(data[Bound.START:Bound.STOP:Bound.STEP], data[start:stop:step])
print(data[False:True:True], data[(None):(None):(None)])
#> B'D\XFFB' B'D\XFFB'
#> B'A' B'AB\X00\XFFCD'

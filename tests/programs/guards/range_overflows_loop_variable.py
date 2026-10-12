# fails: BRK guard
from pyc64.types import i16, u8
i: u8 = 0
start: i16 = 255
stop: i16 = 257
for i in range(start, stop):
    print(i)
print("unreachable")
#> 255

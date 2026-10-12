# oracle: cpython
from pyc64.types import i16, u8, u16

def show(start: i16, stop: i16, step: i16):
    count: u16 = 0
    for i in range(start, stop, step):
        print(i)
        count += 1
    print("count", count)

show(2, 11, 3)
#> 2
#> 5
#> 8
#> COUNT 3
show(8, -3, -4)
#> 8
#> 4
#> 0
#> COUNT 3
show(8, 3, 1)
#> COUNT 0
show(32760, 32767, 10)
#> 32760
#> COUNT 1
show(-32768, 32767, 30000)
#> -32768
#> -2768
#> 27232
#> COUNT 3
show(32767, -32768, -32768)
#> 32767
#> -1
#> COUNT 2
stop: u16 = 3
start: u8 = 1
for j in range(start, stop):
    print(j)
#> 1
#> 2
for k in range(3):
    print(k)
#> 0
#> 1
#> 2

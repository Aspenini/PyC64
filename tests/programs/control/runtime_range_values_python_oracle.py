from pyc64.types import u16, u8
stop: u16 = 5
start: u8 = 2
for i in range(stop):
    print(i)
for j in range(start, stop):
    print(j)
#> 0
#> 1
#> 2
#> 3
#> 4
#> 2
#> 3
#> 4

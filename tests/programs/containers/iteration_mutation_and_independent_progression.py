# oracle: cpython
from pyc64.types import u16, u8
values = [1, 2, 3]
position: u8 = 0
item: u16 = 10
for item in values:
    print(item)
    if position == 0:
        values[2] = 7
    item = 99
    position += 1
print(item, values[-1], position)
def change(buffer: list[u8]):
    buffer[1] = 42
for observed in values:
    print(observed)
    change(values)
#> 1
#> 2
#> 7
#> 99 7 3
#> 1
#> 42
#> 7

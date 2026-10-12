# oracle: cpython
from pyc64.types import u16, array

def add(a: u16, b: u16) -> u16:
    return a + b

def twice(a: u16) -> u16:
    temporary: u16 = 0
    temporary += a
    return temporary * 2

values: array[u16, 10] = array(u16, 10)
total: u16 = 0
for i in range(10):
    values[i] = twice(u16(i))
    total = add(total, values[i])
print(total, add(add(2, 3), add(4, 5)), len(values), values[9])
#> 90 14 10 18

def show(value: u16) -> u16:
    print("inner", value)
    return value + 1

print("outer", show(1), show(2), show(65534))
#> INNER 1
#> INNER 2
#> INNER 65534
#> OUTER 2 3 65535

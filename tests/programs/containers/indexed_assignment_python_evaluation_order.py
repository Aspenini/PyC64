# oracle: cpython
# The right-hand side runs before the subscript, as in Python.
from pyc64.types import u8, u16

def right(values) -> u16:
    print("right")
    values[0] = 1
    return 500

def index(values) -> u8:
    print("index")
    return u8(values[0])

values: list[u16] = [0, 10]
values[index(values)] = right(values)
print(values[0], values[1])
#> RIGHT
#> INDEX
#> 1 500

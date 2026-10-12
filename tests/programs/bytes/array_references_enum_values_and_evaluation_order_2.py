# oracle: cpython
from pyc64.types import array, i8
def capture(values: array[i8]):
    return bytes(values)
values = array(i8, 3)
values[1] = 127
print(capture(values))
#> B'\X00\X7F\X00'

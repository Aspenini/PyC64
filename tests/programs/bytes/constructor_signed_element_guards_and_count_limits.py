# oracle: cpython
from pyc64.types import i8
values = [i8(0), i8(127)]
result = bytes(values)
print(result)
#> B'\X00\X7F'

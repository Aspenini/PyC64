from pyc64.types import array, u16
from exports import modify
values: array[u16, 300] = array(u16, 300)
count = modify(values)
print(count, values[255], values[256], values[299])
#> 300 1234 1235 65535

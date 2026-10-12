# oracle: cpython
from pyc64.types import u8, array

flag: bool = True
if flag:
    value = 7
else:
    value = 9
values = array(u8, 3)
values[1] = 9
values = array(u8, 3)
empty: str = ""
print(value, values[1], len(empty))
#> 7 0 0
print(empty)
#>

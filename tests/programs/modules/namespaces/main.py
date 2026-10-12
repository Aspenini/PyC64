# Each module has its own globals; aliases and from-imports share them.
# oracle: cpython
import left as first, right
from left import add as left_add, value as imported
from pyc64.types import u16
value: u16 = 100

def add(a: u16) -> u16:
    return a + value

print(first.add(right.add(3)), add(left_add(imported)), right.value + first.value)
print(first.value, right.value, value)
#> 33 120 30
#> 10 20 100

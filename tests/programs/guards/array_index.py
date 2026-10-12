# fails: BRK guard
from pyc64.types import array, u8
values: array[u8, 2] = array(u8, 2)
index: u8 = 2
print(values[index])
print("unreachable")

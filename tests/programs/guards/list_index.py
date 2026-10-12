# fails: BRK guard
from pyc64.types import i16
values = [1, 2, 3]
index: i16 = -4
print(values[index])
print("unreachable")

# fails: BRK guard
from pyc64.types import i8
a: i8 = -1
b: i8 = 0
print(a % b)
print("unreachable")

# fails: BRK guard
from pyc64.types import u16
a: u16 = 1
b: u16 = 0
print(a // b)
print("unreachable")

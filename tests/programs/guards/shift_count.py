# fails: BRK guard
from pyc64.types import u8
a: u8 = 1
b: u8 = 16
print(a << b)
print("unreachable")

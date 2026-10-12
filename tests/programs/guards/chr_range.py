# fails: BRK guard
from pyc64.types import i16
value: i16 = 128
print(chr(value))
print("unreachable")

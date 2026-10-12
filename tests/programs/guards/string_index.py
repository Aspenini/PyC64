# fails: BRK guard
from pyc64.types import i16
text = "abc"
index: i16 = -4
print(text[index])
print("unreachable")

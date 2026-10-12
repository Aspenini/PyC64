# fails: BRK guard
from pyc64.types import i16
step: i16 = 0
for i in range(0, 1, step):
    pass
print("unreachable")

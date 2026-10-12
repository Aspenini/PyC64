# fails: BRK guard
from pyc64.types import u16
stop: u16 = 32768
for i in range(stop):
    pass
print("unreachable")

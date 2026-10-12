# An out-of-range store traps only after its value has been evaluated.
# fails: BRK guard
from pyc64.types import i16, u8

def right() -> u8:
    print("evaluated")
    return 42

def write(values, index: i16):
    values[index] = right()

values = [10, 20]
write(values, 2)
print("unreachable")
#> EVALUATED

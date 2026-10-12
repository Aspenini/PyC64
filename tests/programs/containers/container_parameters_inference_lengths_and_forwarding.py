from pyc64.types import u16, u8
def outer(values):
    return inner(values)
def inner(values):
    values[-1] = values[0]
    return len(values)
def size(values: list[u8]) -> u16:
    return len(values)
short = [10]
long = [20, 30, 40]
empty = []
first = outer(short)
second = outer(long)
nothing = size(empty)
print(first, second, nothing, long[2])
#> 1 3 0 20

from pyc64.types import u16, u8
def change(values, value: u8) -> u16:
    values[0] = value
    return len(values)
first = [1]
second = [2, 3]
result = change(first, u8(change(second, 7)))
def local(value: u8) -> u8:
    values = [1, 2]
    change(values, value)
    return values[0] + values[1]
one = local(8)
two = local(20)
print(result, first[0], second[0], one, two)
#> 1 2 7 10 22

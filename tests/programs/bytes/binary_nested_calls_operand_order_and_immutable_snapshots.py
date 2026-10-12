# oracle: cpython
from pyc64.types import u16
calls = [u16(0)]
def part(value):
    calls[0] = u16(calls[0] * 10 + value)
    return bytes([value, 0])
def count(value):
    calls[0] = u16(calls[0] * 10 + value)
    return value
def combine(a, b):
    return (a + b) * 2
first = part(1) + (part(2) + part(3))
print(first, calls[0])
calls[0] = 0
second = count(2) * part(3)
print(second, calls[0], first)
calls[0] = 0
third = part(1) * count(2)
print(third, calls[0], combine(first, second), first, second)
print(part(4) * count(0), calls[0])
#> B'\X01\X00\X02\X00\X03\X00' 123
#> B'\X03\X00\X03\X00' 23 B'\X01\X00\X02\X00\X03\X00'
#> B'\X01\X00\X01\X00' 12 B'\X01\X00\X02\X00\X03\X00\X03\X00\X03\X00\X01\X00\X02\X00\X03\X00\X03\X00\X03\X00' B'\X01\X00\X02\X00\X03\X00' B'\X03\X00\X03\X00'
#> B'' 1240

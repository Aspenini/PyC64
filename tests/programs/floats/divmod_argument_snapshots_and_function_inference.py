# requires: roms
from pyc64.types import array, u8
values = array(float, 1)
calls = array(u8, 1)
values[0] = 7.5
def left():
    calls[0] = u8(calls[0] + 1)
    return values[0]
def right():
    calls[0] = u8(calls[0] + 1)
    values[0] = 100.0
    return -2.0
def divide(a, b):
    return a // b
def remainder(a, b):
    return a % b
first = divide(left(), right())
values[0] = 7.5
second = left() % right()
third = remainder(divide(17.0, 2.0), 3.0)
print(first, second, third, calls[0])
#> -4.0 -0.5 2.0 4

# requires: roms
from pyc64.types import i16, u8
class Number:
    _value: float = 7.5
    _count: u8 = 0
    @property
    def value(self) -> float:
        self._count += 1
        return self._value
    @value.setter
    def value(self, value: float):
        self._value = value
def divisor(value: Number) -> float:
    value._count += 10
    value._value = 100.0
    return -2.0
number = Number()
number.value //= divisor(number)
first = number._value
number._value = 7.5
number.value %= divisor(number)
second = number._value
number._value /= 2.0
third = number._value
value = 7.5
value //= -2.0
value %= 3.0
value /= 4.0
integer: i16 = -7
integer //= 2
integer %= 3
print(first, second, third, value, integer, number._count)
#> -4.0 -0.5 -0.25 0.5 2 22

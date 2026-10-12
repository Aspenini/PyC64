# oracle: cpython
from pyc64.types import u8
class Counter:
    _value: u8
    def __init__(self):
        self._value = 1
        return None
    @property
    def value(self) -> u8:
        return self._value
    @value.setter
    def value(self, value: u8) -> None:
        self._value = value
        return (None)
        self._value = 99
def update(values, stop):
    if stop:
        return None
    values[0] = values[0] + 1
def explicit(values: list[u8]) -> None:
    values[0] = values[0] + 2
    return (None)
    values[0] = values[0] + 20
values = [1]
update(values, True)
update(values, False)
explicit(values)
counter = Counter()
counter.value = values[0]
print(values[0], counter.value)
#> 4 4

# oracle: cpython
from pyc64.types import u16
class Meter:
    _value: u16 = 10
    _label: str = "start"

    @property
    def value(self):
        print("get", self._value)
        return self._value

    @value.setter
    def value(self, value):
        print("set", value)
        self._value = value

    @property
    def label(self) -> str:
        return self._label

    @label.setter
    def label(self, value: str):
        self._label = value

    @property
    def active(self) -> bool:
        return self._value > 0

def rhs(value):
    print("rhs", value)
    return value

first = Meter()
second = Meter()
first.value = rhs(300)
first.value += rhs(5)
first.label = "updated"
print(first.value, second.value, first.label, first.active)
#> RHS 300
#> SET 300
#> GET 300
#> RHS 5
#> SET 305
#> GET 305
#> GET 10
#> 305 10 UPDATED TRUE

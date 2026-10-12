# requires: roms
class P:
    _value: float = 1.5
    @property
    def value(self):
        return self._value
    @value.setter
    def value(self, value: float):
        self._value = value

def change(values: list[float]):
    values[-1] = values[0] / 2

p = P()
p.value += 2.5
values = [p.value, 2, 3.5]
change(values)
for value in values:
    print(value)
print(p.value)
#> 4.0
#> 2.0
#> 2.0
#> 4.0

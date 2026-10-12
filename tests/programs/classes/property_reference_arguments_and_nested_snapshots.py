# oracle: cpython
from pyc64.types import u16
class P:
    _value: u16 = 1
    @property
    def value(self):
        return self._value
    @value.setter
    def value(self, value):
        self._value = value

def change(p, value):
    p.value = value
    return p.value

def combine(first, second):
    return first + second

p = P()
q = P()
result = combine(p.value, change(p, 500))
print(result, p.value, q.value)
#> 501 500 1

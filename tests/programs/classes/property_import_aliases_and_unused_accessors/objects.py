from pyc64.types import u16
class P:
    _value: u16 = 2
    @property
    def value(self):
        return self._value
    @value.setter
    def value(self, value):
        self._value = value
    @property
    def unused(self):
        return unknown()
    @unused.setter
    def unused(self, value):
        unknown(value)

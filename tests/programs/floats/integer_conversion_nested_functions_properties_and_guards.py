# oracle: cpython
class Reading:
    _value: float = -12.75
    @property
    def value(self) -> float:
        print("get")
        return self._value
def real() -> float:
    print("real")
    return 100.9
def combine(first: int, second: int) -> int:
    return first + second
reading = Reading()
value = combine(int(reading.value), int(real()))
reading._value = 32767.75
high = int(reading.value)
reading._value = -32768.75
low = int(reading.value)
small = -0.9
zero = int(small)
print(value, high, low, zero)
#> GET
#> REAL
#> GET
#> GET
#> 88 32767 -32768 0

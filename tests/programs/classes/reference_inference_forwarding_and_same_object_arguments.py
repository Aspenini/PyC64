# oracle: cpython
from pyc64.types import u16
class Counter:
    value: u16 = 0
    def add(self, amount: u16):
        self.value += amount
    def total(self) -> u16:
        return self.value

def outer(counter, amount):
    inner(counter, amount)
    return counter.total()

def inner(counter, amount):
    counter.add(amount)

def twice(first: Counter, second: Counter):
    first.add(1)
    second.add(2)

counter = Counter()
other = Counter()
print(outer(counter, 500), outer(other, 10))
twice(counter, counter)
Counter.add(other, 20)
print(counter.value, other.value)
#> 500 10
#> 503 30

# requires: roms
from pyc64.types import array
values = array(float, 70)
def value():
    print("value")
    return 1.25
def index():
    print("index")
    return 69
values[index()] = value()
def update(values: array[float]):
    values[69] = values[69] * 2
update(values)
last = values[69]
print(last, values[0])
current: float = 0.0
for current in [1, 2]:
    print(current)
#> VALUE
#> INDEX
#> 2.5 0.0
#> 1.0
#> 2.0

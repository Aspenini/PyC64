# oracle: cpython
from pyc64.types import i16, u8
for value in [1, 2, 3]:
    if value == 2:
        continue
    print(value)
else:
    print("list exhausted")
for word in ["a", "b"]:
    print(word)
    break
else:
    print("unreachable")
for index in range(0,):
    print("unreachable")
else:
    print("empty range")
stop: i16 = 4
for index in range(1, stop, 1,):
    if index == 2:
        continue
    print(index)
else:
    print("runtime range exhausted")
remaining: u8 = 2
while remaining:
    remaining -= 1
    continue
else:
    print("while exhausted")
while True:
    break
else:
    print("unreachable")
while False:
    pass
else:
    print("false condition")
#> 1
#> 3
#> LIST EXHAUSTED
#> A
#> EMPTY RANGE
#> 1
#> 3
#> RUNTIME RANGE EXHAUSTED
#> WHILE EXHAUSTED
#> FALSE CONDITION

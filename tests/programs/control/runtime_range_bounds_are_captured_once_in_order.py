from pyc64.types import i16
def bound(value: i16) -> i16:
    print("bound", value)
    return value
for i in range(bound(1), bound(6), bound(2)):
    print(i)
stop: i16 = 6
step: i16 = 2
for j in range(0, stop, step):
    print(j)
    stop = 1
    step = 0
    j = 100
#> BOUND 1
#> BOUND 6
#> BOUND 2
#> 1
#> 3
#> 5
#> 0
#> 2
#> 4

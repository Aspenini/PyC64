# oracle: cpython
from pyc64.random import seed, next_u16
for initial in [0, 1, 42, 65535]:
    seed(initial)
    for index in range(4):
        print(next_u16())
seed(42)
first = next_u16()
seed(42)
print(first == next_u16())
#> 59735
#> 6811
#> 30695
#> 37471
#> 63305
#> 9023
#> 35259
#> 40208
#> 31248
#> 2748
#> 18844
#> 49370
#> 19811
#> 391
#> 51620
#> 11735
#> TRUE

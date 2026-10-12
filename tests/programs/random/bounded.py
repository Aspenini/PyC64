# oracle: cpython
from pyc64.random import seed, randbelow, randrange, randint, next_u16
seed(42)
for bound in [1, 3, 6, 255, 256, 1000, 32769, 65535]:
    print(randbelow(bound), randbelow(bound))
print(next_u16())
for index in range(4):
    print(randrange(-32768, 32767), randrange(-10, 11), randrange(5, 6))
    print(randint(-32768, 32767), randint(-1, 0), randint(32767, 32767))
#> 0 0
#> 1 2
#> 4 2
#> 216 182
#> 85 16
#> 640 774
#> 29534 29724
#> 62637 36788
#> 37943
#> 7686 -9 5
#> 24151 -1 32767
#> 21120 -8 5
#> 21881 -1 32767
#> -7855 2 5
#> -12302 0 32767
#> 18103 9 5
#> -20485 -1 32767

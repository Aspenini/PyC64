# oracle: cpython
from pyc64.types import array, u16
numbers = [1, 500, 65535]
signed = [-128, 127, 250]
flags = [False, True]
words = ["Yes", "Indeed"]
for number in numbers:
    print(number)
for negative in signed:
    print(negative)
for flag in flags:
    print(flag)
for word in words:
    print(word)
for temporary in [10, 20, 30]:
    print(temporary)
for zero in array(u16, 3):
    print(zero)
#> 1
#> 500
#> 65535
#> -128
#> 127
#> 250
#> FALSE
#> TRUE
#> YES
#> INDEED
#> 10
#> 20
#> 30
#> 0
#> 0
#> 0

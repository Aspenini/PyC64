# oracle: cpython
from pyc64.types import u8, i16, array

values: array[i16, 4] = array(i16, 4)
values[0] = -5
values[3] = 300
for value in values:
    print(value)
#> -5
#> 0
#> 0
#> 300
numbers = [3, 1, 4]
total = 0
for number in numbers:
    numbers[2] = 9
    total += number
print(total)
#> 13
for letter in "abc":
    print(letter)
#> A
#> B
#> C
flags = [True, False]
for flag in flags:
    print(not flag)
#> FALSE
#> TRUE

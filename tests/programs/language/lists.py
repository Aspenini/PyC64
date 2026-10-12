# oracle: cpython
from pyc64.types import u8, i16

scores = [10, 20, 30]
scores[1] = 25
print(len(scores), scores[0], scores[1], scores[-1])
#> 3 10 25 30
signed: list[i16] = [-1, -300, 7]
signed[2] = signed[0] + signed[1]
print(signed[2])
#> -301
flags = [True, False, True]
count: u8 = 0
for flag in flags:
    if flag:
        count += 1
print(count)
#> 2
first = [1, 2]
first = [5, 6]
print(first[0] + first[1])
#> 11

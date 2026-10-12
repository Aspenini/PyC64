# oracle: cpython
# A generic library function takes one element type per program.
import pyc64.random as rng
from pyc64.random import choice as choose
from pyc64.types import i16, u16, array
rng.seed(42)
words = [i16(-200), i16(500), i16(1000)]
for index in range(8):
    print(choose(words))
#> -200
#> -200
#> 500
#> 1000
#> 500
#> 1000
#> -200
#> 1000

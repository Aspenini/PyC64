# oracle: cpython
# A generic library function takes one element type per program.
import pyc64.random as rng
from pyc64.random import choice as choose
from pyc64.types import i16, u16, array
rng.seed(42)
words = array(u16, 3)
words[0] = 5
words[1] = 300
words[2] = 65535
for index in range(8):
    print(choose(words))
#> 5
#> 5
#> 300
#> 65535
#> 300
#> 65535
#> 5
#> 65535

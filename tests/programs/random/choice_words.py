# oracle: cpython
# A generic library function takes one element type per program.
import pyc64.random as rng
from pyc64.random import choice as choose
from pyc64.types import i16, u16, array
rng.seed(42)
words = ["Yes", "Absolutely", "Indeed", "Sure"]
for index in range(8):
    print(choose(words))
#> YES
#> YES
#> YES
#> INDEED
#> INDEED
#> INDEED
#> INDEED
#> INDEED

# oracle: cpython
from pyc64.random import seed, choice
seed(42)
words = ["only"]
print(choice(words))
words[0] = "changed"
print(choice(words), words[0])
#> ONLY
#> CHANGED CHANGED

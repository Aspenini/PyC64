# A failed precondition prints its reason and stops before changing state.
# fails: BRK guard
from pyc64.random import seed, randbelow, randrange, randint, choice
seed(42)
words: list[str] = []
choice(words)
print("unreachable")
#> CHOICE REQUIRES A NONEMPTY SEQUENCE

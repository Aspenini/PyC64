from pyc64.random import seed, choice, randint, randrange, next_u16
from c64.video import screen

screen.clear()
seed(42)
yes_words = ["Yes", "Absolutely", "Indeed", "Sure"]
print("8 ball:", choice(yes_words))
print("Dice:", randint(1, 6), randint(1, 6))
print("Offset:", randrange(-10, 11))
sample = next_u16()
print("Sample:", sample)

# Reseeding reproduces the same first answer.
seed(42)
print("Again:", choice(yes_words))

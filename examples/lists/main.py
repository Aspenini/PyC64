from pyc64.types import i16
from c64.video import screen


def kevinallen(peanut_butter, grouper_sandwich):
    print(peanut_butter, grouper_sandwich)


def doubled(value):
    return value * 2


screen.clear()
yes_words = ["Yes", "Absolutely", "Indeed", "Sure"]
choice: i16 = -2
kevinallen(yes_words[0], yes_words[choice])
yes_words[-1] = "Maybe"
print("Answer:", yes_words[-1], "count:", len(yes_words))

# The later call widens the inferred parameter and return to u16.
small = doubled(12)
large = doubled(300)
print("Doubled:", small, large)

numbers = [10, 20, 30]
numbers = [numbers[2], numbers[0], numbers[1]]
print("Rotated:", numbers[0], numbers[1], numbers[2])

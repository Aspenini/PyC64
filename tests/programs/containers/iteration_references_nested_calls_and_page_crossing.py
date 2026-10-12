# oracle: cpython
from pyc64.types import array, u16
def total(values) -> u16:
    result: u16 = 0
    for value in values:
        result += value
    return result
def forwarded(values):
    return total(values)
values = array(u16, 300)
for index in range(300):
    values[index] = u16(index)
print(forwarded(values))
short = array(u16, 1)
short[0] = 700
print(forwarded(short))
def display(words: list[str]):
    for word in words:
        for character in word:
            print(character)
words = ["ab", "CD"]
display(words)
empty: list[str] = []
display(empty)
#> 44850
#> 700
#> A
#> B
#> C
#> D

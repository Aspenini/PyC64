from pyc64.types import u16
def edit(words: list[str], flags: list[bool], numbers: list[u16]):
    words[-1] = "changed"
    flags[0] = not flags[1]
    numbers[-1] = numbers[0] + 300
def alias(first: list[u16], second: list[u16]):
    first[0] = 500
    second[1] = first[0] + 1
words = ["first", "last"]
flags = [False, False]
numbers: list[u16] = [10, 20, 30]
edit(words, flags, numbers)
alias(numbers, numbers)
print(words[0], words[1], flags[0], numbers[0], numbers[1], numbers[-1])
#> FIRST CHANGED TRUE 500 501 310

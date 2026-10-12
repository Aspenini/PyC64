# pyc64test: --input "ONE\rTWO\r"
def ask(prompt):
    return input(prompt)

first = ask("First? ")
second = ask("Second? ")
for word in [first, second]:
    print(word)
print(first, second, len(first), len(second))
#> FIRST? ONE
#> SECOND? TWO
#> ONE
#> TWO
#> ONE TWO 3 3

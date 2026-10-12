# oracle: cpython
empty = ""
word = "Yes"

def effect():
    print("effect")
    return "Later"

print(bool(empty), bool(word), not empty, not word)
print(empty and effect(), word or effect(), word and effect(), empty or effect())
if empty:
    print("unreachable")
else:
    print("empty")
while word:
    print(word)
    word = ""
else:
    print("exhausted")
#> FALSE TRUE TRUE FALSE
#> EFFECT
#> EFFECT
#>  YES LATER LATER
#> EMPTY
#> YES
#> EXHAUSTED

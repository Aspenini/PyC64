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
if word:
    print("nonempty")
while word:
    print(word)
    word = ""
else:
    print("exhausted")
print("" and 9, "yes" or 9, "yes" and 9, "" or 9)
#> FALSE TRUE TRUE FALSE
#> EFFECT
#> EFFECT
#>  YES LATER LATER
#> EMPTY
#> NONEMPTY
#> YES
#> EXHAUSTED
#>  YES 9 9

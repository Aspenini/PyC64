# DEL edits the line, NUL is ignored and RETURN finishes it.
# pyc64test: --input "\x14\x00AB\x14C\r\rX\r"
first = input("Name? ")
empty = input()
last = input("Again? ")
print(first, len(first), first[-1], empty == "", last)
#> NAME? ABC
#>
#> AGAIN? X
#> AC 2 C TRUE X

# oracle: cpython
from pyc64.types import u16
number: u16 = 99
word = "before"
empty = []
for number in empty:
    print("unreachable")
else:
    print("empty list")
for word in "":
    print("unreachable")
else:
    print("empty string")
for fresh in []:
    print(fresh)
for direct_empty in []:
    pass
print(number, word)
#> EMPTY LIST
#> EMPTY STRING
#> 99 BEFORE

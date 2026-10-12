# oracle: cpython
from pyc64.types import u8
text = "abc"
first = ""
last = ""
position: u8 = 0
def echo(character):
    return character
for character in text:
    if position == 0:
        first = echo(character)
        text = "xyz"
    last = character
    position += 1
print(first, last, text, position)
for next_character in "de":
    print(next_character)
print(first, last)
def original():
    print("capture")
    return "pq"
for returned in original():
    print(returned)
#> A C XYZ 3
#> D
#> E
#> A C
#> CAPTURE
#> P
#> Q

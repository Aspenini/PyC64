# oracle: cpython
from pyc64.types import i16

def text():
    return "Abc"

word = "Abc"
index: i16 = -1
first = text()[0]
last = word[index]
word = "new"
print(first, last, len(word), len(text()), ord(first), ord(last))
for character in "xyz":
    print(character)
print("ABC"[-3], "ABC"[-1], "ABC"[True], len("ABC"[0]))
long_text = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaZ"

def show(position: i16):
    print(len(long_text), long_text[position], ord(long_text[position]))

show(-261)
show(-1)
show(0)
show(255)
show(256)
show(260)
#> A C 3 3 65 99
#> X
#> Y
#> Z
#> A C B 1
#> 261 A 97
#> 261 Z 90
#> 261 A 97
#> 261 A 97
#> 261 A 97
#> 261 Z 90

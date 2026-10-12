# oracle: cpython
from typing import Final
from pyc64.types import u8
YES: Final = "a" == "a"
CASE: Final = "a" == "A"
ORDER: Final = "Z" < "a"
LETTER: Final = ord("a")
FIRST: Final = "Abc"[0]
CHAR: Final = chr(98)
print(YES, CASE, ORDER, LETTER, FIRST, CHAR)
value: u8 = 97
character = chr(value)
saved = character
value = 98
print(character, saved, chr(value), ord(saved), chr(127) > chr(1))
for code in [1, 9, 10, 13, 32, 65, 90, 122, 127]:
    value = code
    print(ord(chr(value)), len(chr(value)))
#> TRUE FALSE TRUE 97 A B
#> A A B 97 TRUE
#> 1 1
#> 9 1
#> 10 1
#> 13 1
#> 32 1
#> 65 1
#> 90 1
#> 122 1
#> 127 1

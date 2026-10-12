# oracle: cpython
from pyc64.types import u8
from typing import Final
YES: Final = "a" == "a"
CASE: Final = "a" == "A"
ORDER: Final = "Z" < "a"
NEWLINES: Final = "\n" != "\r"
EMPTY: Final = not ""
NONEMPTY: Final = bool("a")
LETTER: Final = ord("a")
FIRST: Final = "Abc"[0]
CHAR: Final = chr(98)
print(YES, CASE, ORDER, NEWLINES, EMPTY, NONEMPTY, LETTER, FIRST, CHAR)
value: u8 = 97
character = chr(value)
saved = character
value = 98
other = chr(value)
print(character, saved, other, ord(saved), chr(127) > chr(1))
#> TRUE FALSE TRUE TRUE TRUE TRUE 97 A B
#> A A B 97 TRUE

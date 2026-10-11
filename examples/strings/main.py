from pyc64.types import u16
from pyc64.strings import find, contains, count, startswith, endswith, isdigit
from c64.video import screen


screen.clear()
text = "Hello C64!"
length: u16 = len(text)
found = find(text, "C64")
print("TEXT:", text, "LEN:", length)
print("FIRST:", text[0], "LAST:", text[-1])
print("FIND:", found, "COUNT:", count("banana", "an"))
print("MATCH:", startswith(text, "Hello"), endswith(text, "!"), contains(text, "c64"))
print("CASE:", "Hello" == "hello", "A" < "a")
print("DIGITS:", isdigit("6502"), "ASCII:", ord("a"), chr(97))

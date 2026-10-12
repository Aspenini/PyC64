# oracle: cpython
from pyc64.types import u16
def capture(values):
    return bytes(values)
values = [u16(0), u16(255), u16(42)]
first = capture(values)
values[1] = 65
second = capture(values)
count: u16 = 300
zeroes = bytes(count)
text = "Ascii\nText"
encoded = bytes(text, "UTF_8")
copy = bytes(encoded)
print(first, second, len(zeroes), zeroes[-1], bool(zeroes))
print(encoded, copy, bytes(), bytes(3), bytes([0, True, 255]))
#> B'\X00\XFF*' B'\X00A*' 300 0 TRUE
#> B'ASCII\NTEXT' B'ASCII\NTEXT' B'' B'\X00\X00\X00' B'\X00\X01\XFF'

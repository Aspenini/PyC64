# requires: roms
from pyc64.types import u16, i16
a = 1.25
b: float = 2.5
integer_unsigned: u16 = 65535
integer_signed: i16 = -32768
zero = float()
print(a + b, b - a, a * b, b / a, a + 500, -a, float(integer_unsigned), float(integer_signed), zero)
print(a < b, a <= b, a == b, a != b, a > b, a >= b, b >= b)
print(bool(a), bool(zero), not a, not zero)
print(5 / 2, 0.0 or 1.5, -2.5 and -1.5)
#> 3.75 1.25 3.125 2.0 501.25 -1.25 65535.0 -32768.0 0.0
#> TRUE TRUE FALSE TRUE FALSE FALSE TRUE
#> TRUE FALSE FALSE TRUE
#> 2.5 1.5 -1.5

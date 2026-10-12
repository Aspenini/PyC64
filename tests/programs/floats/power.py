# requires: roms
from pyc64.types import i16, i8

def base() -> float:
    print("base")
    return 2.5

def exponent() -> float:
    print("exponent")
    return 3.0

result = base() ** exponent()
signed: i16 = -2
reciprocal = 2 ** signed
positive: i8 = 3
negative = (-2.0) ** 3
fractional = pow(9.0, 0.5)
augmented = 3.0
augmented **= 2
print(result, reciprocal, negative, fractional, augmented)
print(2 ** positive, 0.0 ** 0, 2 ** -2, 2.0 ** 3 ** 2, -2.0 ** 2)
#> BASE
#> EXPONENT
#> 15.625 0.25 -8.0 3.0 9.0
#> 8.0 1.0 0.25 512.0 -4.0

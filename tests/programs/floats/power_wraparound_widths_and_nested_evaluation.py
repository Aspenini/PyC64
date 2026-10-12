from pyc64.types import i8, u16, u8
def base() -> u16:
    print("base")
    return 3
def exponent() -> u8:
    print("exponent")
    return 2
value = base() ** (base() ** exponent())
small: u8 = 20
wrapped = small ** 2
wide: u16 = small ** 2
minus: i8 = -2
negative = minus ** 7
print(value, wrapped, wide, negative)
#> BASE
#> BASE
#> EXPONENT
#> 19683 144 400 -128

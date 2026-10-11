from pyc64.types import u16, u8
# Fixed-width helpers compiled as ordinary PyC64 functions.

def min_u8(a: u8, b: u8) -> u8:
    if a < b:
        return a
    return b

def max_u8(a: u8, b: u8) -> u8:
    if a > b:
        return a
    return b

def min_u16(a: u16, b: u16) -> u16:
    if a < b:
        return a
    return b

def max_u16(a: u16, b: u16) -> u16:
    if a > b:
        return a
    return b

# The caller supplies low <= high.
def clamp_u8(value: u8, low: u8, high: u8) -> u8:
    if value < low:
        return low
    if value > high:
        return high
    return value

def clamp_u16(value: u16, low: u16, high: u16) -> u16:
    if value < low:
        return low
    if value > high:
        return high
    return value

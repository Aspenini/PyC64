from pyc64.types import u16, u8
multiplier: u16 = 10

def points(round: u8) -> u16:
    return u16(round) * multiplier

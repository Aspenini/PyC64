from pyc64.types import i16, u16

# Two bytes of shared state. Explicit conversions also reproduce the target's
# wrapping arithmetic when this ordinary Python module runs under CPython.
_state = [u16(1)]


def seed(value: u16) -> None:
    _state[0] = u16(value)


def next_u16() -> u16:
    # Full-period 16-bit LCG followed by a reversible output permutation.
    _state[0] = u16(_state[0] * 25173 + 13849)
    value: u16 = u16(_state[0] ^ (_state[0] >> 7))
    value = u16(value * 40503)
    return u16(value ^ (value >> 9))


def randbelow(bound: u16) -> u16:
    assert bound > 0, "randbelow requires a positive bound"
    # Reject the incomplete remainder of the 65536-value output space.
    threshold: u16 = u16(-bound) % bound
    value: u16 = next_u16()
    while value < threshold:
        value = next_u16()
    return value % bound


def randrange(start: i16, stop: i16) -> i16:
    assert start < stop, "randrange requires start < stop"
    width: u16 = u16(u16(stop) - u16(start))
    return i16(u16(start) + randbelow(width))


def randint(low: i16, high: i16) -> i16:
    assert low <= high, "randint requires low <= high"
    width: u16 = u16(u16(high) - u16(low) + 1)
    if width == 0:
        # The complete signed 16-bit interval has 65536 possible values.
        return i16(u16(low) + next_u16())
    return i16(u16(low) + randbelow(width))


def choice(values):
    assert len(values) > 0, "choice requires a nonempty sequence"
    return values[randbelow(len(values))]

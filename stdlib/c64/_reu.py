"""Host equivalents of native REU primitives; supply a c64.memory adapter."""
from c64.memory import peek, poke
from pyc64.types import u8, u16


def present() -> bool:
    low, high = peek(0xDF02), peek(0xDF03)
    poke(0xDF02, 0x55)
    poke(0xDF03, 0xAA)
    first = peek(0xDF02) == 0x55 and peek(0xDF03) == 0xAA
    if not first:
        poke(0xDF03, high)
        poke(0xDF02, low)
        return False
    poke(0xDF02, 0xAA)
    poke(0xDF03, 0x55)
    second = peek(0xDF02) == 0xAA and peek(0xDF03) == 0x55
    poke(0xDF03, high)
    poke(0xDF02, low)
    return first and second


def transfer(c64_address: u16, reu_address: u16, bank: u8, count: u16,
             operation: u8, control: u8) -> u8:
    assert 0 < count <= 65535 and 0 <= operation <= 3
    assert 0 <= control <= 255 and (control & 63) == 0
    poke(0xDF09, 0)
    peek(0xDF00)
    poke(0xDF02, u8(c64_address))
    poke(0xDF03, u8(c64_address >> 8))
    poke(0xDF04, u8(reu_address))
    poke(0xDF05, u8(reu_address >> 8))
    poke(0xDF06, bank)
    poke(0xDF07, u8(count))
    poke(0xDF08, u8(count >> 8))
    poke(0xDF0A, control)
    poke(0xDF01, 0x90 | operation)
    return peek(0xDF00)

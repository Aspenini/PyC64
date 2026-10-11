"""Host equivalents of native sprite primitives; supply a c64.memory adapter."""
from c64.memory import peek, poke
from pyc64.types import u8, u16


def enable(slot: u8) -> None:
    assert 0 <= slot < 8, "sprite index must be 0..7"
    poke(0xD015, peek(0xD015) | (1 << slot))


def disable(slot: u8) -> None:
    assert 0 <= slot < 8, "sprite index must be 0..7"
    poke(0xD015, peek(0xD015) & u8(~(1 << slot)))


def position(slot: u8, x: u16, y: u8) -> None:
    assert 0 <= slot < 8, "sprite index must be 0..7"
    assert 0 <= x < 512, "sprite X coordinate must be 0..511"
    assert 0 <= y < 256, "sprite Y coordinate must be 0..255"
    mask = 1 << slot
    if x >= 256:
        poke(0xD010, peek(0xD010) | mask)
    else:
        poke(0xD010, peek(0xD010) & u8(~mask))
    poke(0xD000 + 2 * slot, u8(x))
    poke(0xD001 + 2 * slot, y)


def color(slot: u8, value: u8) -> None:
    assert 0 <= slot < 8, "sprite index must be 0..7"
    assert 0 <= value < 16, "sprite color must be 0..15"
    poke(0xD027 + slot, value)


def data(slot: u8, address: u16) -> None:
    assert 0 <= slot < 8, "sprite index must be 0..7"
    assert 0 <= address < 16384, "sprite data requires VIC bank 0"
    assert address % 64 == 0, "sprite data must be 64-byte aligned"
    poke(0x07F8 + slot, u8(address // 64))

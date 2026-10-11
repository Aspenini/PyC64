from pyc64.types import u8, u16
from c64.memory import peek, poke
from c64._sprite import enable, disable, position, color, data


def _flag(address: u16, index: u8, value: bool) -> None:
    mask: u8 = u8(1 << index)
    if value:
        poke(address, peek(address) | mask)
    else:
        poke(address, peek(address) & u8(~mask))


def shared_colors(first: u8, second: u8) -> None:
    assert first >= 0 and first < 16 and second >= 0 and second < 16, "sprite colors must be 0..15"
    poke(0xD025, first)
    poke(0xD026, second)


class Sprite:
    _index: u8

    def __init__(self, index: u8):
        assert index >= 0 and index < 8, "sprite index must be 0..7"
        self._index = index

    @property
    def index(self) -> u8:
        return self._index

    @property
    def x(self) -> u16:
        low: u16 = peek(0xD000 + 2 * self._index)
        if peek(0xD010) & u8(1 << self._index):
            return low + 256
        return low

    @x.setter
    def x(self, value: u16):
        position(self._index, value, self.y)

    @property
    def y(self) -> u8:
        return peek(0xD001 + 2 * self._index)

    @y.setter
    def y(self, value: u8):
        position(self._index, self.x, value)

    @property
    def color(self) -> u8:
        return peek(0xD027 + self._index) & 15

    @color.setter
    def color(self, value: u8):
        color(self._index, value)

    @property
    def enabled(self) -> bool:
        return bool(peek(0xD015) & u8(1 << self._index))

    @enabled.setter
    def enabled(self, value: bool):
        if value:
            enable(self._index)
        else:
            disable(self._index)

    def enable(self) -> None:
        enable(self._index)

    def disable(self) -> None:
        disable(self._index)

    def set_data(self, address: u16) -> None:
        data(self._index, address)

    @property
    def multicolor(self) -> bool:
        return bool(peek(0xD01C) & u8(1 << self._index))

    @multicolor.setter
    def multicolor(self, value: bool):
        _flag(0xD01C, self._index, value)

    @property
    def expand_x(self) -> bool:
        return bool(peek(0xD01D) & u8(1 << self._index))

    @expand_x.setter
    def expand_x(self, value: bool):
        _flag(0xD01D, self._index, value)

    @property
    def expand_y(self) -> bool:
        return bool(peek(0xD017) & u8(1 << self._index))

    @expand_y.setter
    def expand_y(self, value: bool):
        _flag(0xD017, self._index, value)

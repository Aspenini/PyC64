"""Fixed-width conversions and fixed arrays; the C64 compiler lowers these APIs.

These Python definitions support ordinary imports, annotations, and host-side
library tests. Python itself does not enforce annotations or Final declarations.
"""

from enum import IntEnum


class u8(int):
    def __new__(cls, value=0):
        return int.__new__(cls, int(value) & 255)


class i8(int):
    def __new__(cls, value=0):
        value = int(value) & 255
        return int.__new__(cls, value - 256 if value >= 128 else value)


class u16(int):
    def __new__(cls, value=0):
        return int.__new__(cls, int(value) & 65535)


class i16(int):
    def __new__(cls, value=0):
        value = int(value) & 65535
        return int.__new__(cls, value - 65536 if value >= 32768 else value)


char = u8


class array:
    """A fixed-length, zero-filled array constructed as array(element_type, count)."""

    @classmethod
    def __class_getitem__(cls, specification):
        sized = isinstance(specification, tuple)
        if sized:
            if len(specification) != 2:
                raise ValueError('array annotation requires an element type and optional count')
            element, count = specification
        else:
            element, count = specification, None
        supported = element in (bool, u8, i8, u16, i16, float) or (isinstance(element, type) and issubclass(element, IntEnum))
        if not supported or (sized and (not isinstance(count, int) or not 1 <= count <= 16384)):
            raise ValueError('array requires an integer or float element type and 1..16384 elements')
        return cls

    def __init__(self, element, count):
        self.__class__[element, count]
        self.element = element
        self._values = [element(0) for _ in range(count)]

    def __len__(self):
        return len(self._values)

    def __getitem__(self, index):
        if not 0 <= index < len(self):
            raise IndexError('array index out of bounds')
        return self._values[index]

    def __setitem__(self, index, value):
        if not 0 <= index < len(self):
            raise IndexError('array index out of bounds')
        self._values[index] = self.element(value)

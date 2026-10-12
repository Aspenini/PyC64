# oracle: cpython
import typing as hints
import pyc64.types as types
from typing import Final as Frozen
from pyc64.types import u8 as Byte

COUNT: hints.Final[Byte] = 4
VALUE: Frozen = 9

def convert(value: types.u16) -> Byte:
    return Byte(value)

values = types.array(Byte, COUNT)
values[3] = convert(265)
result: Byte = values[3] + VALUE
print(result, len(values))
#> 18 4

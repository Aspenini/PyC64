from enum import IntEnum as Base, auto as next_value
from typing import Final
START: Final = 7
class Color(Base):
    RED = START
    GREEN = next_value()
    BLUE = next_value()
DEFAULT: Final[Color] = Color.GREEN
def pick(value: Color) -> Color:
    return value

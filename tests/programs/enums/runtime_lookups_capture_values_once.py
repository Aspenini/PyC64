# oracle: cpython
from pyc64.types import array, i16, u8
import enum as e
class Direction(e.IntEnum):
    LEFT = -300
    RIGHT = 400
    EAST = RIGHT
calls = array(u8, 1)
def number():
    calls[0] = u8(calls[0] + 1)
    return i16(-300)
def word():
    calls[0] = u8(calls[0] + 1)
    return "EAST"
value = Direction(number())
alias = Direction[word()]
print(value.name, value.value, alias.name, alias.value, calls[0])
print(Direction(number()).name, Direction[word()].value, calls[0])
#> LEFT -300 RIGHT 400 2
#> LEFT 400 4

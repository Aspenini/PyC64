from typing import Final
from pyc64.types import i16, u16
FIRST: Final = b"A\x00\xffAB".find(b"AB")
LAST: Final[i16] = b"A\x00\xffAB".rfind(65)
COUNT: Final[u16] = b"aaaaa".count(b"aa")

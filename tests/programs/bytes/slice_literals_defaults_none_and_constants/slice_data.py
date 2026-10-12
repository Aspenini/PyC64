from typing import Final
DATA: Final[bytes] = b"A\x00\xffBC"
REVERSE: Final = DATA[::-1]
MIDDLE: Final = DATA[1:-1]

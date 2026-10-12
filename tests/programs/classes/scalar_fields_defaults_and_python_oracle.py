# oracle: cpython
from pyc64.types import i16, i8, u16, u8
from typing import Final
BASE: Final[u16] = 500
class Record:
    byte: u8 = 5
    delta: i8 = -20
    word: u16 = BASE + 3
    offset: i16 = -300
    ready: bool = True
    text: str = "Mixed"

    def adjust(self, amount: i8) -> i16:
        self.delta = i8(self.delta + amount)
        self.offset -= 10
        self.text = "Updated"
        self.ready = False
        return self.offset

record = Record()
other: Record = Record()
result = record.adjust(2)
print(record.byte, record.delta, record.word, record.offset, record.ready, record.text, result)
print(other.delta, other.offset, other.ready, other.text)
#> 5 -18 503 -310 FALSE UPDATED -310
#> -20 -300 TRUE MIXED

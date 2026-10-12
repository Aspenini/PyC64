from pyc64.types import array, u16
from typing import Final
COUNT: Final[u16] = 300
def edit(values: array[u16, COUNT]) -> u16:
    values[255] = 1234
    values[256] = values[255] + 1
    values[299] = 65535
    return len(values)
def forward(values: array[u16]):
    return edit(values)

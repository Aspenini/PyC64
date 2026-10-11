from pyc64.types import u8, u16, array
from pyc64.collections import copy_into, fill
from c64.video import screen


def increment(values):
    for index in range(len(values)):
        values[index] = values[index] + 1


def rename(words: list[str]):
    words[-1] = "Changed"


screen.clear()
source = [10, 20, 30]
destination = array(u16, 5)
fill(destination, u16(7))
copied: u16 = copy_into(source, destination, 3)
increment(destination)
words = ["First", "Last"]
rename(words)
print("Copied:", copied, "values:", destination[0], destination[2], destination[4])
print("Words:", words[0], words[-1])

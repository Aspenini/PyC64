from pyc64.types import i16, u16
from c64.video import screen

screen.clear()
data = b"\x00\xffAB\x00\xffAB\x00"
needle = b"\x00\xff"
first: i16 = data.find(needle)
last: i16 = data.rfind(needle)
indexed: i16 = data.index(b"AB")
reverse_indexed: i16 = data.rindex(b"AB")
occurrences: u16 = data.count(needle)
zeroes: u16 = data.count(0)
empty_count: u16 = data.count(b"")
prefix: bool = data.startswith(needle)
suffix: bool = data.endswith(b"AB\x00")
print("Positions:", first, last, indexed, reverse_indexed)
print("Counts:", occurrences, zeroes, empty_count)
print("Edges:", prefix, suffix)
print("Window:", data.find(needle, 1, -1), data.find(b"ab"))
present: bool = needle in data
absent: bool = 254 not in data
print("Membership:", present, absent)
assert data.find(255) == 1
assert data.rfind(0) == 8
assert data.count(b"", 99) == 0
assert data.startswith(b"", len(data))
assert b"" in data
assert b"ab" not in data

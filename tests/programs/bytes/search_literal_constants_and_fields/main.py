from pyc64.types import i16, u16
from search_data import FIRST, LAST, COUNT
class Result:
    position: i16 = b"abc".index(b"b")
    total: u16 = b"abc".count(b"")
result = Result()
print(FIRST, LAST, COUNT, result.position, result.total)
print(b"abc".find(b""), b"abc".rfind(b""), b"abc".count(b"", 4))
print(b"abc".startswith(b"", 4), b"abc".endswith(b"", 3), b"abc".find(255))
#> 3 3 2 1 4
#> 0 3 0
#> FALSE TRUE -1

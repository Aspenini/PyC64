# A module imported along several paths initializes exactly once.
from one import get
import two
import base as again
from pyc64.types import u8
result: u8 = get() + two.x
print(result, again.runs)
#> 14 1

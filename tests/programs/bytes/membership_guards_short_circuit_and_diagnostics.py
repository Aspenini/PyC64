# oracle: cpython
from pyc64.types import u16
needle: u16 = 256
data = b"ABC"
a = False
b = True
print(a and needle in data, b or needle not in data)
#> FALSE TRUE

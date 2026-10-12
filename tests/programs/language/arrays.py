# oracle: cpython
from pyc64.types import u8, u16, array

big: array[u8, 257] = array(u8, 257)
big[256] = 42
print(big[0], big[256], len(big))
#> 0 42 257

def fill(buffer: array[u16], value: u16):
    for i in range(len(buffer)):
        buffer[i] = value + u16(i)

words = array(u16, 3)
fill(words, 1000)
print(words[0], words[1], words[2])
#> 1000 1001 1002
words = array(u16, 3)
print(words[1])
#> 0

# oracle: cpython
from data import buffer, message
from pyc64.types import u8
import trailing
buffer[1] = 9
x: u8 = buffer[2] + buffer[1]
print(x, message(), trailing.value)
#> 16 MODULE WORKS 9

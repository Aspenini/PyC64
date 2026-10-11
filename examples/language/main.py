from pyc64.types import array, u16
from c64.video import screen

def add(a: u16, b: u16) -> u16:
    return a + b

values: array[u16, 10] = array(u16, 10)
total: u16 = 0

for i in range(10):
    values[i] = u16(i) * 10
    total = add(total, values[i])

if total == 450:
    screen.border = 5
    print("NATIVE CODE WORKS")
else:
    screen.border = 2
    print("UNEXPECTED RESULT")

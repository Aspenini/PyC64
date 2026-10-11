from pyc64.types import i16, u16
from c64.video import screen

screen.clear()
dividend: i16 = -7
divisor: i16 = 3
quotient: i16 = dividend // divisor
remainder: i16 = dividend % divisor
print("Division:", quotient, remainder)

stop: i16 = 8
step: i16 = 2
total: i16 = 0
for value in range(-3, stop, step):
    total += value
    # range captured stop and step before the first iteration.
    stop = 0
    step = 0
print("Total:", total)

start: i16 = 32760
limit: i16 = 32767
stride: i16 = 10
count: u16 = 0
for value in range(start, limit, stride):
    count += 1
print("Edge count:", count)

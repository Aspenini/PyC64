# oracle: cpython
from pyc64.types import u16

count: u16 = 0
for i in range(256):
    count += 1
for j in range(10, -1, -2):
    if j == 6:
        continue
    count += 2
    if j == 2:
        break
while count > 260:
    count -= 1
if count < 260:
    result = 0
elif count == 260:
    result = 1
else:
    result = 2
print(count, result)
#> 260 1
n: u16 = 0
while True:
    n += 3
    if n > 10:
        break
print(n)
#> 12

# oracle: cpython
from pyc64.types import u16
source = b"\x00\xffA"
total: u16 = 0
for value in source:
    source = b"changed"
    total += value
    print(value)
else:
    print("done")
for value in b"\x00\x01":
    if value == 0:
        continue
    for nested in b"AB":
        print(value, nested)
        break
    else:
        print("wrong")
print(source, total)
#> 0
#> 255
#> 65
#> DONE
#> 1 65
#> B'CHANGED' 320

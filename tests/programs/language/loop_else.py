# oracle: cpython
from pyc64.types import u8

def search(target: u8) -> u8:
    for value in range(5):
        if value == target:
            print("found", value)
            break
    else:
        print("missing", target)
        return 0
    return 1

print(search(3), search(9))
#> FOUND 3
#> MISSING 9
#> 1 0
n: u8 = 0
while n < 3:
    n += 1
else:
    print("while done", n)
#> WHILE DONE 3
for outer in range(2):
    for inner in range(3):
        if inner == outer:
            break
    else:
        print("never")
    print("outer", outer)
#> OUTER 0
#> OUTER 1
for empty in range(0):
    pass
else:
    print("empty else")
#> EMPTY ELSE

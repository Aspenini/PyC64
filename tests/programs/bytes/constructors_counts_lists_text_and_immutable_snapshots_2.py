# oracle: cpython
def make(a, b):
    return bytes([a, b, True])
first = make(0, 255)
second = make(42, 65)
print(first, second)
#> B'\X00\XFF\X01' B'*A\X01'

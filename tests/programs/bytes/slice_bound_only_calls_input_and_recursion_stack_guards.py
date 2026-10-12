# oracle: cpython
def stop():
    return 4
def step():
    return -1
data = b"ABCDE"
print(data[:stop()], data[::step()])
#> B'ABCD' B'EDCBA'

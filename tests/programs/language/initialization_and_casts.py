from pyc64.types import array, i8, u16, u8
empty: str = ""
print(empty)
a: array[u8, 257] = array(u8, 257)
a[256] = 42
x: i8 = i8(128)
y: u16 = u16(-1)
z: bool = bool(y)
t: bool = True
sum: u8 = t + t
neg: i8 = -t
#>

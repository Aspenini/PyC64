# requires: roms
def make(value) -> bytes:
    return bytes([value, 255])
def real() -> float:
    return 1.25
print(make(1), real(), make(2), make(3))
#> B'\X01\XFF' 1.25 B'\X02\XFF' B'\X03\XFF'

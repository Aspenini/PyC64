from binary_data import DATA, RAW, SIZE
copy: bytes = DATA
raw: bytes = RAW
print(copy, raw, SIZE, DATA[-1])
#> B'\X00\XFF*' B'\\X41' 3 42

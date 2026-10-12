from slice_data import DATA, REVERSE, MIDDLE
copy = DATA[None:None:None]
print(DATA[:], REVERSE, MIDDLE, copy)
print(DATA[-1::-2], DATA[:-1:-1], DATA[-1:None:-1], DATA[99:-99:-3])
print(DATA[-99:99], DATA[4:1], DATA[1:3:65535], DATA[::-32768])
#> B'A\X00\XFFBC' B'CB\XFF\X00A' B'\X00\XFFB' B'A\X00\XFFBC'
#> B'C\XFFA' B'' B'CB\XFF\X00A' B'C\X00'
#> B'A\X00\XFFBC' B'' B'\X00' B'C'

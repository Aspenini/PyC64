# oracle: cpython
data = b"\x00\xffAb\x00\xffAb\x00"
needle = b"\x00\xff"
print(data.find(needle), data.rfind(needle), data.index(needle), data.rindex(needle))
print(data.count(needle), data.count(0), data.count(b""), data.find(255), data.rfind(0))
print(data.startswith(needle), data.endswith(b"b\x00"), data.find(b"ab"), data.find(b"Ab"))
repeat = b"aaaaa"
print(repeat.find(b"aa"), repeat.rfind(b"aa"), repeat.count(b"aa"))
#> 0 4 0 4
#> 2 3 10 1 8
#> TRUE TRUE -1 2
#> 0 3 2

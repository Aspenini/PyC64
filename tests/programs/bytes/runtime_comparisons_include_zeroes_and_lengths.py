# oracle: cpython
a = b"ASCII"
b = "ASCII"
print(a == b, a != b, a == 65, 65 != a)
print((a or b"fallback"), (b"" and a), (a and b"other"))
#> FALSE TRUE FALSE TRUE
#> B'ASCII' B'' B'OTHER'

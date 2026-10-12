# oracle: cpython
one = br"\t\x00"
two = RB"\t\x00"
three = b"\a\b\f\v\r\t\n\1\12\123\x7f\xff"
print(one, two, three, one == two)
#> B'\\T\\X00' B'\\T\\X00' B'\X07\X08\X0C\X0B\R\T\N\X01\NS\X7F\XFF' TRUE

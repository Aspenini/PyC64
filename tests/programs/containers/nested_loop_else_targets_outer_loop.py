# oracle: cpython
for outer in [1, 2, 3]:
    for inner in [4, 5]:
        print(outer, inner)
        break
    else:
        print("unreachable")
else:
    print("outer exhausted")
for outer in range(3):
    while False:
        pass
    else:
        print("outer break", outer)
        break
else:
    print("unreachable")
for outer in [1, 2]:
    for inner in []:
        pass
    else:
        continue
    print("unreachable")
else:
    print("outer continued")
#> 1 4
#> 2 4
#> 3 4
#> OUTER EXHAUSTED
#> OUTER BREAK 0
#> OUTER CONTINUED

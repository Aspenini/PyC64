# oracle: cpython
values = [10, 20]
def effect(number):
    print(number)
    values[0] = 99
    return number
for value in [effect(values[0]), effect(values[1]), values[0]]:
    print(value)
#> 10
#> 20
#> 10
#> 20
#> 99

values = [10, 20]
def item(value):
    print(value)
    values[0] = 99
    return value
values = [item(values[1]), values[0]]
print(values[0], values[1])
#> 20
#> 20 99

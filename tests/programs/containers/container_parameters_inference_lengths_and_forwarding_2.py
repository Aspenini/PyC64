def echo(value):
    return value
def head(values):
    return values[0]
values = [echo(1), echo(500)]
result = head(values)
print(result, values[1])
#> 1 500

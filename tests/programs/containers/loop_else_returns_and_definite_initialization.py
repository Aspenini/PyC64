# oracle: cpython
def find(values):
    for value in values:
        if value > 5:
            return value
    else:
        return 0
def while_result(flag):
    while flag:
        return 7
    else:
        return 9
def nested_result():
    for outer in [1]:
        for inner in [2]:
            break
    else:
        return 10
small = [1, 2]
large = [1, 9]
print(find(small), find(large), while_result(False), while_result(True), nested_result())
for value in []:
    pass
else:
    initialized = 42
print(initialized)
while False:
    pass
else:
    another = 10
print(another)
#> 0 9 9 7 10
#> 42
#> 10

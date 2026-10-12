# requires: roms
def twice(value):
    return value * 2
def add(first, second):
    return first + second
first = twice(1.25)
second = twice(300)
print(first, second, add(twice(1.5), twice(2.5)), twice(4.5))
print(twice(1.5) + twice(2.5), twice(5.0), twice(6.0))
#> 2.5 600.0 8.0 9.0
#> 8.0 10.0 12.0

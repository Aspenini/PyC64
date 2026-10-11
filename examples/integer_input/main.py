import math

count = int(input("Count? "))
total: int = 0
for index in range(count):
    total += index
print("Total:", total)

value = -12.75
print("Integer:", int(value))
print("Floor:", math.floor(value))
print("Ceiling:", math.ceil(value))
print("Truncated:", math.trunc(value))

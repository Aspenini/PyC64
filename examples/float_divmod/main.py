value = -7.5
divisor = 2.0
quotient = value // divisor
remainder = value % divisor
decimal = 1.0 // 0.1
huge = 1e38 % 3.0

print("Floor:", quotient)
print("Remainder:", remainder)
print("Decimal:", decimal)
print("Huge modulo:", huge)

value //= divisor
value %= 3.0
value /= 4.0
print("Updated:", value)

from c64.video import screen


def kevinallen(peanut_butter, grouper_sandwich):
    winston = peanut_butter * 0 + 134
    hohoho = grouper_sandwich * 0 + 2
    return winston / hohoho


screen.clear()
first = float(input("First number? "))
second = float(input("Second number? "))
result = kevinallen(first, second)
quotient = first / second
print("Result:", result)
print("Division:", quotient)

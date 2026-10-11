from pyc64.types import u16, array
from c64.video import screen


def sum_values(values) -> u16:
    total: u16 = 0
    for value in values:
        total += value
    return total


screen.clear()
numbers = [10, 20, 30]
total = sum_values(numbers)
print("SUM:", total)

for word in ["Yes", "Absolutely", "Indeed", "Sure"]:
    print(word)
else:
    print("ALL ANSWERS")

scores = array(u16, 3)
scores[0] = 100
scores[1] = 300
scores[2] = 500
for score in scores:
    if score > 200:
        print("FOUND:", score)
        break
else:
    print("NO SCORE")

saved = ""
position: u16 = 0
for character in "C64":
    if position == 0:
        saved = character
    print(character)
    position += 1
print("SAVED:", saved)

remaining: u16 = 2
while remaining > 0:
    remaining -= 1
else:
    print("FINISHED")

import c64.keyboard as keyboard
from c64.video import screen

screen.clear()
keyboard.clear()
print("Press keys; Q quits")
while True:
    key = keyboard.read()
    if key == 81:
        break
    if key != 0:
        print("Key:", key)
print("Done")

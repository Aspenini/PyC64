from pyc64.types import u8
from c64.video import screen
from c64.input import joystick

x: u8 = 20
y: u8 = 12

screen.border = 6
screen.background = 0
screen.clear()

while True:
    screen.put_char(x, y, " ")
    if joystick.left and x > 0:
        x -= 1
    if joystick.right and x < 39:
        x += 1
    if joystick.up and y > 0:
        y -= 1
    if joystick.down and y < 24:
        y += 1
    screen.put_char(x, y, "@")

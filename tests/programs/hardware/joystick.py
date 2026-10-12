from c64.input import joystick
from c64.memory import poke
poke(0xDC00, 0xFB)
print(joystick.port2.left, joystick.right, joystick.up, joystick.fire)
poke(0xDC00, 0x6E)
print(joystick.port2.left, joystick.up, joystick.fire)
#> TRUE FALSE FALSE FALSE
#> FALSE TRUE TRUE

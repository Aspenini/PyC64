import c64.keyboard as keyboard
from c64.memory import peek, poke
poke(0xC6, 3)
poke(0x277, 65)
poke(0x278, 66)
poke(0x279, 67)
first = keyboard.read()
second = keyboard.read()
keyboard.clear()
print(first, second, keyboard.read(), peek(198))
#> 65 66 0 0

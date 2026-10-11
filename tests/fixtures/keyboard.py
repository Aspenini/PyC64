from c64.keyboard import read, clear
from c64.memory import poke

# Seed the normal KERNAL queue to make this integration check deterministic.
poke(631, 65)
poke(632, 66)
poke(633, 67)
poke(198, 3)
first = read()
second = read()
clear()
empty = read()
print("Keys:", first, second, empty)

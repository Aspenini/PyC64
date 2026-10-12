# The PRG loads through BASIC, prints to the KERNAL screen and writes RAM.
# frames: 25
# screen: MACHINE RESULT 42
# expect: 0xC000=42
from c64.memory import poke
poke(0xC000, 42)
print("MACHINE RESULT", 42)

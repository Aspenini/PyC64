from c64.memory import peek, poke
poke(49152, 42)
poke(49153, peek(49152) + 1)
print(peek(49152), peek(49153))
# expect: 0xC000=42
# expect: 0xC001=43
#> 42 43

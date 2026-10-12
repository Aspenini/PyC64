from c64 import sprite as sprites
from c64.memory import poke
poke(0xD006, 44)
poke(0xD007, 55)
poke(0xD010, 8)
poke(0xD02A, 0xF3)
first = sprites.Sprite(3)
second = sprites.Sprite(3)
print(first.x, first.y, first.color)
sprites.position(3, 400, 90)
sprites.color(3, 6)
sprites.enable(3)
print(second.x, second.y, second.color, second.enabled)
first.x -= 1
print(second.x, second.y)
second.enabled = False
print(first.enabled)
#> 300 55 3
#> 400 90 6 TRUE
#> 399 90
#> FALSE

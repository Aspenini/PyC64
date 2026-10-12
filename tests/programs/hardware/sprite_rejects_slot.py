# The constructor rejects a slot outside 0..7 before touching hardware.
# fails: BRK guard
from c64.sprite import Sprite
p = Sprite(8)
#> SPRITE INDEX MUST BE 0..7

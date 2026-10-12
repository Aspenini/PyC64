# Two modules may bind the same local name to different libraries.
import first, second
from c64.memory import peek, poke
poke(0xDC00, 0xFB)
first.update()
print(peek(0xD020), second.pressed())
#> 6 TRUE

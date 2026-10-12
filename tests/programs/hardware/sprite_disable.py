from c64.sprite import disable, position
from c64.memory import poke
poke(0xD015, 255)
poke(0xD010, 255)
disable(5)
position(5, 0, 0)
# expect: 0xD015=0xDF
# expect: 0xD010=0xDF
# expect: 0xD00A=0
# expect: 0xD00B=0

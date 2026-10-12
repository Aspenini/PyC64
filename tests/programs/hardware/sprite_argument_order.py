# Arguments are snapshotted left to right before the hardware write.
import c64.sprite as sprite
from pyc64.types import u8, u16

def nested(value: u16) -> u16:
    sprite.position(7, 500, 80)
    print(value)
    return value

sprite.position(1, nested(300), u8(nested(50)))
# expect: 0xD002=44
# expect: 0xD003=50
# expect: 0xD00E=244
# expect: 0xD00F=80
# expect: 0xD010=130
#> 300
#> 50

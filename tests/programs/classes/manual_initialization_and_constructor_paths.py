# oracle: cpython
from pyc64.types import u16, u8
class Point:
    x: u8
    y: u8
    def sum(self) -> u16:
        return u16(self.x) + u16(self.y)

class Conditional:
    value: u8
    def __init__(self, choose: bool):
        if choose:
            self.value = 10
        else:
            self.value = 20
        return

point = Point()
point.x = 200
point.y = 100
left = Conditional(True)
right = Conditional(False)
print(point.sum(), left.value, right.value)
#> 300 10 20

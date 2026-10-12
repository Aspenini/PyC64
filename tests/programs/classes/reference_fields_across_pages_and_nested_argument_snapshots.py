# oracle: cpython
from pyc64.types import u16
class Item:
    value: u16 = 10
    def show(self, first: u16, second: u16) -> u16:
        print(first, second, self.value)
        return self.value

def change(item: Item) -> u16:
    item.value = 500
    return 3

item = Item()
print(item.show(item.value, change(item)))
#> 10 3 500
#> 500

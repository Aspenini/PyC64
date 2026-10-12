from pyc64.types import u16
class Item:
    value: u16
    def __init__(self, value: u16):
        self.value = value
    def get(self) -> u16:
        return self.value
owned = Item(500)

from pyc64.types import u8
from c64.video import screen


class Player:
    x: u8
    y: u8
    health: u8
    name: str = "Player"

    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.health = 100

    def damage(self, amount: u8):
        self.health -= amount

    def move(self, dx: u8, dy: u8):
        self.x = u8(self.x + dx)
        self.y = u8(self.y + dy)

    def alive(self) -> bool:
        return self.health > 0


def heal(player: Player, amount: u8):
    player.health += amount


screen.clear()
player = Player(10, 20)
other = Player(80, 90)
player.damage(25)
player.move(3, 4)
heal(player, 5)
other.damage(100)
print(player.name, player.x, player.y, player.health, player.alive())
print(other.name, other.x, other.y, other.health, other.alive())

# oracle: cpython
from pyc64.types import u16
events = [0]
class Pair:
    first: u16
    second: u16
    label: str = "Pair"
    def __init__(self, first: u16, second: u16):
        events[0] = events[0] + 1
        print("init", first, second)
        self.first = first
        self.second = second

def argument(value: u16) -> u16:
    events[0] = events[0] + 1
    print("arg", value)
    return value

def make(value: u16) -> u16:
    local = Pair(argument(value), argument(u16(value + 1)))
    print(local.first, local.second, local.label)
    local.label = "Changed"
    return local.second

print(make(10), make(500), events[0])
#> ARG 10
#> ARG 11
#> INIT 10 11
#> 10 11 PAIR
#> ARG 500
#> ARG 501
#> INIT 500 501
#> 500 501 PAIR
#> 11 501 6

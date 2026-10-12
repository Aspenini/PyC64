# oracle: cpython
from pyc64.types import array
from enum import IntEnum
class State(IntEnum):
    IDLE = 0
    ACTIVE = 300
class Device:
    _state: State = State.IDLE
    @property
    def state(self) -> State:
        return self._state
    @state.setter
    def state(self, value: State):
        self._state = value
def choose(flag):
    if flag:
        return State.ACTIVE
    return State.IDLE
def identity(value):
    return value
def fill(values: list[State], value: State):
    for index in range(len(values)):
        values[index] = value
def write(values: array[State]):
    values[1] = State.ACTIVE
device = Device()
device.state = choose(True)
items: list[State] = [State.IDLE, State.ACTIVE]
fill(items, identity(State.ACTIVE))
buffer = array(State, 2)
write(buffer)
print(device.state.name, device._state.name, items[0].name, buffer[1].name)
print(identity(items[1]).value, buffer[0].name)
#> ACTIVE ACTIVE ACTIVE ACTIVE
#> 300 IDLE

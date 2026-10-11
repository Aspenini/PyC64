from enum import IntEnum, auto
from pyc64.types import array, u16


class Mode(IntEnum):
    OFF = 0
    READY = auto()
    PLAY = auto()
    PLAYING = PLAY
    PAUSED = auto()


def next_mode(current: Mode) -> Mode:
    if current == Mode.READY:
        return Mode.PLAY
    return Mode.READY


current = next_mode(Mode.READY)
name = "PLAYING"
selected = Mode[name]
history = array(Mode, 2)
history[1] = selected
total: u16 = 0
print("Mode:", current.name, current.value)
print("Alias:", selected.name)
for mode in Mode:
    print(mode.name, mode.value)
    total += mode.value
print("Count:", len(Mode), "Total:", total)
print("History:", history[0].name, history[1].name)

# oracle: cpython
from enum import IntEnum, auto
from typing import Final
BASE: Final = 5
class Mode(IntEnum):
    OFF = 0
    RUN = BASE
    RUNNING = RUN
    WAIT = auto()
    LOW = 2
    NEXT = auto()
current: Mode = Mode.RUNNING
print(current, current.name, current.value, Mode.RUNNING.name, len(Mode))
print(Mode["RUNNING"].name, Mode(6).name)
for member in Mode:
    print(member.name, member.value)
else:
    print("done")
#> 5 RUN 5 RUN 5
#> RUN WAIT
#> OFF 0
#> RUN 5
#> WAIT 6
#> LOW 2
#> NEXT 7
#> DONE

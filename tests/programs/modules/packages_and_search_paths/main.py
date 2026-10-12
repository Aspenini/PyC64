# Dotted imports resolve packages; -I adds a search directory.
# pyc64c: -I "extra modules"
import package.helper
import package.helper as helper
import c64.video as video
import other
from pyc64.types import u8
from c64.memory import peek
result: u8 = package.helper.number() + other.value
video.screen.border = helper.number()
print(result, peek(0xD020))
#> 45 42

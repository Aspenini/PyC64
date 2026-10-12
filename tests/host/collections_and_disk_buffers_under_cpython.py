"""Host-side check (CPython) migrated from test_collections_and_disk_buffers_under_cpython."""
import sys, types
from pyc64.types import array, u8, u16
from pyc64.collections import fill, copy_into
def annotated(buffer: array[u8]):
    return len(buffer)
kernal = types.ModuleType("c64.kernal")
state = {"status": 0, "position": 0, "writes": [], "reads": 0}
data = [0, 255, 42]
def read_byte(logical):
    value = data[state["position"]]
    state["position"] += 1
    state["reads"] += 1
    state["status"] = 64 if state["position"] == len(data) else 0
    return value
def write_byte(logical, value):
    state["writes"].append(value)
    state["status"] = 0
    return True
for name in ("open", "close", "write_text"):
    setattr(kernal, name, lambda *args: True)
kernal.read_byte = read_byte
kernal.write_byte = write_byte
kernal.status = lambda: state["status"]
kernal.error = lambda: 0
sys.modules["c64.kernal"] = kernal
import c64.disk as disk
target = array(u8, 8)
assert annotated(target) == 8
try:
    array[u8, None]
except ValueError:
    pass
else:
    raise AssertionError("explicit array dimensions must be integers")
fill(target, u8(99))
assert disk.write_from(2, data, 3) == 3
assert disk.read_into(3, target, 3) == 3
assert disk.eof() and not disk.failed()
assert [target[i] for i in range(8)] == [0, 255, 42, 99, 99, 99, 99, 99]
assert disk.read_into(3, target, 0) == 0 and state["reads"] == 3
assert state["writes"] == data
wide = array(u16, 5)
assert copy_into(data, wide, 9) == 3
assert [wide[i] for i in range(5)] == [0, 255, 42, 0, 0]
assert disk.write_from(2, [10, 256, 42], 3) == 1

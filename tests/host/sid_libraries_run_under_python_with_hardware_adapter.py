"""Host-side check (CPython) migrated from test_sid_libraries_run_under_python_with_hardware_adapter."""
import sys, types
registers = {}
memory = types.ModuleType("c64.memory")
memory.poke = lambda address, value: registers.__setitem__(int(address), int(value))
memory.peek = lambda address: registers.get(int(address), 0)
sys.modules["c64.memory"] = memory
from c64 import sid, audio
audio.reset()
assert audio.volume(10)
assert audio.note_on(0, 7493, audio.TRIANGLE, 2, 4, 12, 5)
assert [registers[0xD400+i] for i in range(7)] == [69, 29, 0, 0, 17, 36, 197]
assert audio.note_off(0) and registers[0xD404] == 16
assert sid.filter(1023, 8, 1, sid.LOW_PASS) and registers[0xD418] == 26
before = registers.copy()
assert not audio.note_on(3, 1, 16, 2, 4, 12, 5)
assert registers == before

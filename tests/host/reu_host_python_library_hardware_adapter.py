"""Host-side check (CPython) migrated from test_reu_host_python_library_hardware_adapter."""
import sys, types
from _reu_model import REUMemory
memory = types.ModuleType('c64.memory')
device = REUMemory([0] * 65536)
memory.peek = lambda address: device[address]
memory.poke = lambda address, value: device.__setitem__(address, int(value))
sys.modules['c64.memory'] = memory
from c64 import reu
assert reu.present()
device[0xC000:0xC006] = [0, 255, 42, 128, 254, 1]
assert reu.store(0xC000, 65532, 2, 6)
assert reu.fetch(0xC100, 65532, 2, 6)
assert reu.verify(0xC100, 65532, 2, 6)
device[0xC100] = 99
assert not reu.verify(0xC100, 65532, 2, 6)
device[0xC200:0xC206] = range(1, 7)
assert reu.swap(0xC200, 65532, 2, 6)
assert bytes(device[0xC200:0xC206]) == bytes([0, 255, 42, 128, 254, 1])
assert device.expansion[0x2fffc:0x30002] == bytes(range(1, 7))
device[0xC400] = 85
assert reu.fill(0xC400, 256, 4, 16)
assert device.expansion[0x40100:0x40110] == b'\x55' * 16
before = len(device.writes)
for parameters in [(-1, 0, 0, 1, 0, 0), (0, -1, 0, 1, 0, 0), (0, 0, 256, 1, 0, 0),
                   (0, 0, 0, -1, 0, 0), (0, 0, 0, 65536, 0, 0), (0, 0, 0, 1, 4, 0),
                   (0, 0, 0, 1, 0, 1), (65535, 0, 0, 2, 1, 0), (0, 65535, 255, 2, 0, 0)]:
    assert reu.transfer(*parameters) is False
assert reu.store(65535, 65535, 255, 0)
assert len(device.writes) == before
device.attached = False
assert not reu.present() and not reu.store(0xC000, 0, 0, 1)

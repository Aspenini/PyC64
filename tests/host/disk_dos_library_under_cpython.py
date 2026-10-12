"""Host-side check (CPython) migrated from test_disk_dos_library_under_cpython."""
import sys
import types
kernal = types.ModuleType('c64.kernal')
state = {'status': 0, 'error': 0, 'record': b'', 'position': 0, 'write_ok': True}
def read_byte(logical):
    pos = state['position']
    state['position'] += 1
    if pos >= len(state['record']):
        state['status'] = 2
        return 0
    state['status'] = 64 if state['position'] == len(state['record']) else 0
    return state['record'][pos]
def write_text(logical, text):
    state['command'] = (logical, text)
    return state['write_ok']
kernal.read_byte = read_byte
kernal.write_text = write_text
kernal.status = lambda: state['status']
kernal.error = lambda: state['error']
for name in ('open', 'close', 'write_byte'):
    setattr(kernal, name, lambda *args: None)
sys.modules['c64.kernal'] = kernal
import c64.disk as disk
for code in range(100):
    record = f'{code:02},MESSAGE,12,34\r'.encode()
    state.update(record=record, position=0, error=0)
    assert disk.dos_code(15) == code
    assert state['position'] == len(record)
for record in (b'X0,OK,00,00\r', b'00,,00,00\r', b'00,OK,00,00', b'X' * 80):
    state.update(record=record, position=0)
    assert disk.dos_code(15) == disk.DOS_UNAVAILABLE
    assert state['position'] == min(len(record), 64)
state.update(record=b'00,OK,00,00\r', position=0)
assert disk.command(15, 'r0:new=old') == disk.DOS_OK
assert state['command'] == (15, 'r0:new=old')
state.update(position=0, write_ok=False)
assert disk.command(15, 'r0:new=old') == disk.DOS_UNAVAILABLE
assert state['position'] == 0

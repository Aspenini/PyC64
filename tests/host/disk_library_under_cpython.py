"""Host-side check (CPython) migrated from test_disk_library_under_cpython."""
import sys
import types
kernal = types.ModuleType('c64.kernal')
state = {'status': 0, 'error': 0}
kernal.status = lambda: state['status']
kernal.error = lambda: state['error']
for name in ('open', 'close', 'read_byte', 'write_byte', 'write_text'):
    setattr(kernal, name, lambda *args: None)
sys.modules['c64.kernal'] = kernal
import c64.disk as disk
assert disk.open is kernal.open
for status, error, eof, failed in [(0, 0, False, False), (64, 0, True, False),
                                  (65, 0, True, True), (128, 0, False, True),
                                  (0, 3, False, True)]:
    state.update(status=status, error=error)
    assert disk.eof() == eof
    assert disk.failed() == failed

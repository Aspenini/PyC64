"""Host-side check (CPython) migrated from test_python_library_imports_and_explicit_array_conversions."""
from typing import Final
from pyc64.types import u8, i8, u16, i16, array
from pyc64.math import clamp_u8
assert [u8(256), i8(255), u16(-1), i16(65535)] == [0, -1, 65535, -1]
values: array[u8, 3] = array(u8, 3)
values[1] = 265
assert len(values) == 3 and values[0] == 0 and values[1] == 9
assert clamp_u8(u8(100), u8(5), u8(10)) == 10
real_values = array(float, 2)
real_values[1] = 1.25
assert real_values[0] == 0.0 and real_values[1] == 1.25

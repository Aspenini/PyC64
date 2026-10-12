"""Host-side check (CPython) migrated from test_integer_conversion_diagnostics_and_host_rounding_exports."""
from pyc64._math_library import floor, ceil, trunc
assert [floor(-1.5), ceil(-1.5), trunc(-1.5)] == [-2, -1, -1]

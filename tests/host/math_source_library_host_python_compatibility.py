"""Host-side check (CPython) migrated from test_math_source_library_host_python_compatibility."""
import math
from pyc64 import _math_library as m
assert math.__name__ == "math"
assert m.pi == math.pi
assert m.sqrt(9) == 3
assert m.pow(2, -2) == .25
assert m.radians(180) == math.pi
assert m.degrees(math.pi) == 180
assert m.log2(8) == 3
assert m.log10(100) == 2

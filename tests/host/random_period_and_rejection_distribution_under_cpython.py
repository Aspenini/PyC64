"""Host-side check (CPython) migrated from test_random_period_and_rejection_distribution_under_cpython."""
from pyc64 import random
random.seed(0)
seen = set()
for index in range(65536):
    value = random.next_u16()
    assert 0 <= value <= 65535
    seen.add(value)
assert len(seen) == 65536
assert random._state[0] == 0
for bound in [1, 3, 6, 255, 256, 1000, 32769, 65535]:
    threshold = (65536 - bound) % bound
    counts = [0] * bound
    for value in seen:
        if value >= threshold:
            counts[value % bound] += 1
    assert min(counts) == max(counts)

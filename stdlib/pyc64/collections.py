from pyc64.types import u16

# Ordinary Python operations on fixed containers. The compiler infers the
# element/container types from callers; no target-side Python runtime is used.


def fill(values, value) -> None:
    for index in range(len(values)):
        values[index] = value


def copy_into(source, destination, count: u16) -> u16:
    # Copy a bounded prefix. This copies elements rather than aliasing storage.
    limit: u16 = len(source)
    if len(destination) < limit:
        limit = len(destination)
    if count < limit:
        limit = count
    for index in range(limit):
        destination[index] = source[index]
    return limit

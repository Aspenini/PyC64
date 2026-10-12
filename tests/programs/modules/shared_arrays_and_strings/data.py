from pyc64.types import array, u8
buffer: array[u8, 4] = array(u8, 4)
buffer[2] = 7

def message() -> str:
    return "module works"

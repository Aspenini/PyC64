def start(tune: bytes, song: int) -> bool:
    raise RuntimeError("SID execution needs a C64 adapter under CPython")

def stop() -> bool:
    raise RuntimeError("SID execution needs a C64 adapter under CPython")

def active() -> bool:
    raise RuntimeError("SID execution needs a C64 adapter under CPython")

def ticks() -> int:
    raise RuntimeError("SID execution needs a C64 adapter under CPython")

# VBI/CIA-timed PSID or native-IRQ RSID playback. The tune owns the SID until stop().
# frames() counts PSID player calls or wrapped native IRQ dispatches for RSID.
# Song zero means the file's default; explicit songs use 1-based numbering.
from pyc64.types import u16
from c64.assets import sid as load
from c64._music import start, active as playing, ticks as frames
from c64._music import stop as _stop
import c64.sid as sid


def play(tune: bytes) -> bool:
    return start(tune, 0)


def play_song(tune: bytes, song: u16) -> bool:
    return start(tune, song)


def stop() -> bool:
    stopped: bool = _stop()
    if stopped:
        sid.reset()
    return stopped


def songs(tune: bytes) -> u16:
    if len(tune) < 118:
        return 0
    return (u16(tune[14]) << 8) | u16(tune[15])


def default_song(tune: bytes) -> u16:
    if len(tune) < 118:
        return 0
    return (u16(tune[16]) << 8) | u16(tune[17])

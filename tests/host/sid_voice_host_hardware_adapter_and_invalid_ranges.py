"""Host-side check (CPython) migrated from test_sid_voice_host_hardware_adapter_and_invalid_ranges."""
import types, sys
registers = {}
reads = []
memory = types.ModuleType("c64.memory")
def peek(address):
    assert not 0xD400 <= address <= 0xD418
    reads.append(address)
    return 1 if address == 0x02A6 else 0
memory.peek = peek
memory.poke = lambda address, value: registers.__setitem__(int(address), int(value))
sys.modules["c64.memory"] = memory
from c64.audio import sid, note_on_hz
assert sid.voice is sid.Voice
sid.reset()
first = sid.voice(0)
other = sid.Voice(0)
first.frequency = 440
assert other.frequency_word == 7493
assert abs(other.frequency - 7493 * 985248 / 16777216) < 1e-9
first.set_envelope(1, 2, 3, 4)
other.attack = 5
assert first.attack == 5 and first.decay == 2
first.control = sid.PULSE | sid.SYNC
first.gate = True
assert other.control == 67
before = registers.copy()
for call in [lambda: sid.frequency(-1, 1), lambda: sid.frequency(0, -1),
             lambda: sid.pulse_width(0, -1), lambda: sid.control(0, 256),
             lambda: sid.volume(-1), lambda: sid.filter(-1, 0, 0, 0),
             lambda: note_on_hz(0, float("nan"), 16, 1, 2, 3, 4)]:
    assert call() is False
    assert registers == before
for field, value in [("pulse_width", 4096), ("attack", 16), ("frequency", -1),
                     ("frequency", float("inf")), ("waveform", 17)]:
    try:
        setattr(first, field, value)
    except AssertionError:
        pass
    else:
        raise AssertionError(field)
    assert registers == before
sid.reset()
assert other.frequency_word == 0 and other.control == 0 and other.attack == 0
assert reads and set(reads) == {0x02A6}

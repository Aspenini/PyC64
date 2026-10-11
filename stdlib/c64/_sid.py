from typing import Final
from pyc64.types import u8, u16, array
from c64.memory import poke, peek

# SID registers are write-only. Preserve control bits using software shadows.
_controls = array(u8, 3)
_master = array(u8, 1)
_frequencies = array(u16, 3)
_pulses = array(u16, 3)
_attack_decay = array(u8, 3)
_sustain_release = array(u8, 3)
_clock_mode = array(u8, 1)
_custom_clock = array(float, 1)

AUTO: Final[u8] = 0
PAL: Final[u8] = 1
NTSC: Final[u8] = 2
PAL_CLOCK: Final[float] = 985248.0
NTSC_CLOCK: Final[float] = 1022730.0
_PHASE_SCALE: Final[float] = 16777216.0

GATE: Final[u8] = 1
SYNC: Final[u8] = 2
RING_MOD: Final[u8] = 4
TEST: Final[u8] = 8
TRIANGLE: Final[u8] = 16
SAWTOOTH: Final[u8] = 32
PULSE: Final[u8] = 64
NOISE: Final[u8] = 128
LOW_PASS: Final[u8] = 16
BAND_PASS: Final[u8] = 32
HIGH_PASS: Final[u8] = 64
VOICE3_OFF: Final[u8] = 128


def reset():
    for offset in range(25):
        poke(54272 + u16(offset), 0)
    for voice in range(3):
        _controls[voice] = 0
        _frequencies[voice] = 0
        _pulses[voice] = 0
        _attack_decay[voice] = 0
        _sustain_release[voice] = 0
    _master[0] = 0


def frequency(voice: u8, word: u16) -> bool:
    # Raw SID frequency word, not Hz; tuning depends on the PAL/NTSC clock.
    if voice < 0 or voice > 2 or word < 0 or word > 65535:
        return False
    _frequencies[voice] = word
    address: u16 = 54272 + u16(voice) * 7
    poke(address, u8(word))
    poke(address + 1, u8(word >> 8))
    return True


def pulse_width(voice: u8, width: u16) -> bool:
    if voice < 0 or voice > 2 or width < 0 or width > 4095:
        return False
    _pulses[voice] = width
    address: u16 = 54274 + u16(voice) * 7
    poke(address, u8(width))
    poke(address + 1, u8(width >> 8))
    return True


def control(voice: u8, bits: u8) -> bool:
    if voice < 0 or voice > 2 or bits < 0 or bits > 255:
        return False
    _controls[voice] = bits
    poke(54276 + u16(voice) * 7, bits)
    return True


def gate(voice: u8, enabled: bool) -> bool:
    if voice < 0 or voice > 2:
        return False
    bits: u8 = _controls[voice] & 254
    if enabled:
        bits |= GATE
    return control(voice, bits)


def envelope(voice: u8, attack: u8, decay: u8, sustain: u8, release: u8) -> bool:
    if voice < 0 or voice > 2 or attack < 0 or attack > 15 or decay < 0 or decay > 15 or sustain < 0 or sustain > 15 or release < 0 or release > 15:
        return False
    _attack_decay[voice] = (attack << 4) | decay
    _sustain_release[voice] = (sustain << 4) | release
    address: u16 = 54277 + u16(voice) * 7
    poke(address, (attack << 4) | decay)
    poke(address + 1, (sustain << 4) | release)
    return True


def volume(level: u8) -> bool:
    if level < 0 or level > 15:
        return False
    _master[0] = (_master[0] & 240) | level
    poke(54296, _master[0])
    return True


def filter(cutoff: u16, resonance: u8, routes: u8, modes: u8) -> bool:
    # routes: voice bits 0..2 and external input bit 3; modes occupy bits 4..7.
    if cutoff < 0 or cutoff > 2047 or resonance < 0 or resonance > 15 or routes < 0 or routes > 15 or modes < 0 or modes > 255 or (modes & 15) != 0:
        return False
    poke(54293, u8(cutoff & 7))
    poke(54294, u8(cutoff >> 3))
    poke(54295, (resonance << 4) | routes)
    _master[0] = (_master[0] & 15) | modes
    poke(54296, _master[0])
    return True


def set_clock(mode: u8) -> bool:
    if mode < 0 or mode > NTSC:
        return False
    _clock_mode[0] = mode
    return True


def set_clock_hz(value: float) -> bool:
    if not (value >= 1.0 and value <= _PHASE_SCALE):
        return False
    _custom_clock[0] = value
    _clock_mode[0] = 3
    return True


def clock_hz() -> float:
    mode: u8 = _clock_mode[0]
    if mode == PAL:
        return PAL_CLOCK
    if mode == NTSC:
        return NTSC_CLOCK
    if mode == 3:
        return _custom_clock[0]
    # The KERNAL records PAL (1) or NTSC (0) during machine startup.
    if peek(0x02A6) != 0:
        return PAL_CLOCK
    return NTSC_CLOCK


def word_to_hz(word: u16) -> float:
    assert word >= 0 and word <= 65535, "SID frequency word must be 0..65535"
    return float(word) * clock_hz() / _PHASE_SCALE


def _hz_word(value: float) -> u16:
    return u16(value * _PHASE_SCALE / clock_hz() + 0.5)


def hz_to_word(value: float) -> u16:
    assert value >= 0 and value <= word_to_hz(65535), "SID frequency is outside the selected clock's range"
    return _hz_word(value)


def frequency_hz(voice: u8, value: float) -> bool:
    if voice < 0 or voice > 2:
        return False
    if not (value >= 0 and value <= word_to_hz(65535)):
        return False
    return frequency(voice, _hz_word(value))

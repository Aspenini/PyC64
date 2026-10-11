from pyc64.types import u8, u16
import c64.sid as sid
import c64.music as music
from c64.sid import reset, volume, TRIANGLE, SAWTOOTH, PULSE, NOISE


def _valid_note(voice: u8, waveform: u8, attack: u8, decay: u8,
                sustain: u8, release: u8) -> bool:
    if voice < 0 or voice > 2:
        return False
    if (waveform != sid.TRIANGLE and waveform != sid.SAWTOOTH and
            waveform != sid.PULSE and waveform != sid.NOISE):
        return False
    if attack < 0 or attack > 15 or decay < 0 or decay > 15 or sustain < 0 or sustain > 15 or release < 0 or release > 15:
        return False
    return True


def note_on(voice: u8, word: u16, waveform: u8,
            attack: u8, decay: u8, sustain: u8, release: u8) -> bool:
    # Check everything before changing any register. Word is a raw SID frequency.
    if word < 0 or word > 65535 or not _valid_note(voice, waveform, attack, decay, sustain, release):
        return False
    # Re-trigger the envelope even if the voice was already gated on.
    sid.gate(voice, False)
    sid.envelope(voice, attack, decay, sustain, release)
    sid.frequency(voice, word)
    if (waveform & sid.PULSE) != 0:
        sid.pulse_width(voice, 2048)
    return sid.control(voice, waveform | sid.GATE)


def note_off(voice: u8) -> bool:
    return sid.gate(voice, False)


def note_on_hz(voice: u8, value: float, waveform: u8,
               attack: u8, decay: u8, sustain: u8, release: u8) -> bool:
    if not _valid_note(voice, waveform, attack, decay, sustain, release):
        return False
    if not (value >= 0 and value <= sid.word_to_hz(65535)):
        return False
    return note_on(voice, sid.hz_to_word(value), waveform, attack, decay, sustain, release)

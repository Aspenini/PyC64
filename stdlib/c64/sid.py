# Public SID library: functions and Voice handles share one register shadow.
from c64._sid import reset, frequency, pulse_width, control, gate, envelope, volume, filter
from c64._sid import GATE, SYNC, RING_MOD, TEST, TRIANGLE, SAWTOOTH, PULSE, NOISE
from c64._sid import LOW_PASS, BAND_PASS, HIGH_PASS, VOICE3_OFF
from c64._sid import AUTO, PAL, NTSC, PAL_CLOCK, NTSC_CLOCK
from c64._sid import set_clock, set_clock_hz, clock_hz, word_to_hz, hz_to_word, frequency_hz
from c64._sid_voice import Voice, Voice as voice

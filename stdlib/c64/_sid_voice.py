from pyc64.types import u8, u16
import c64._sid as _sid


def _flag(index: u8, mask: u8, value: bool) -> None:
    bits: u8 = _sid._controls[index]
    if value:
        bits |= mask
    else:
        bits &= u8(~mask)
    assert _sid.control(index, bits), "SID voice index must be 0..2"


class Voice:
    _index: u8

    def __init__(self, index: u8):
        assert index >= 0 and index < 3, "SID voice index must be 0..2"
        self._index = index

    @property
    def index(self) -> u8:
        return self._index

    @property
    def frequency(self) -> float:
        return _sid.word_to_hz(self.frequency_word)

    @frequency.setter
    def frequency(self, value: float):
        assert _sid.frequency_hz(self._index, value), "SID frequency is outside the selected clock's range"

    @property
    def frequency_word(self) -> u16:
        return _sid._frequencies[self._index]

    @frequency_word.setter
    def frequency_word(self, value: u16):
        assert _sid.frequency(self._index, value), "SID frequency word must be 0..65535"

    @property
    def pulse_width(self) -> u16:
        return _sid._pulses[self._index]

    @pulse_width.setter
    def pulse_width(self, value: u16):
        assert _sid.pulse_width(self._index, value), "SID pulse width must be 0..4095"

    @property
    def control(self) -> u8:
        return _sid._controls[self._index]

    @control.setter
    def control(self, value: u8):
        assert _sid.control(self._index, value), "SID control must be 0..255"

    @property
    def waveform(self) -> u8:
        return self.control & 240

    @waveform.setter
    def waveform(self, value: u8):
        assert value >= 0 and value <= 240 and (value & 15) == 0, "SID waveform uses bits 4..7"
        self.control = (self.control & 15) | value

    @property
    def gate(self) -> bool:
        return bool(self.control & _sid.GATE)

    @gate.setter
    def gate(self, value: bool):
        _flag(self._index, _sid.GATE, value)

    @property
    def sync(self) -> bool:
        return bool(self.control & _sid.SYNC)

    @sync.setter
    def sync(self, value: bool):
        _flag(self._index, _sid.SYNC, value)

    @property
    def ring_mod(self) -> bool:
        return bool(self.control & _sid.RING_MOD)

    @ring_mod.setter
    def ring_mod(self, value: bool):
        _flag(self._index, _sid.RING_MOD, value)

    @property
    def test(self) -> bool:
        return bool(self.control & _sid.TEST)

    @test.setter
    def test(self, value: bool):
        _flag(self._index, _sid.TEST, value)

    @property
    def attack(self) -> u8:
        return _sid._attack_decay[self._index] >> 4

    @attack.setter
    def attack(self, value: u8):
        assert _sid.envelope(self._index, value, self.decay, self.sustain, self.release), "SID envelope fields must be 0..15"

    @property
    def decay(self) -> u8:
        return _sid._attack_decay[self._index] & 15

    @decay.setter
    def decay(self, value: u8):
        assert _sid.envelope(self._index, self.attack, value, self.sustain, self.release), "SID envelope fields must be 0..15"

    @property
    def sustain(self) -> u8:
        return _sid._sustain_release[self._index] >> 4

    @sustain.setter
    def sustain(self, value: u8):
        assert _sid.envelope(self._index, self.attack, self.decay, value, self.release), "SID envelope fields must be 0..15"

    @property
    def release(self) -> u8:
        return _sid._sustain_release[self._index] & 15

    @release.setter
    def release(self, value: u8):
        assert _sid.envelope(self._index, self.attack, self.decay, self.sustain, value), "SID envelope fields must be 0..15"

    def set_envelope(self, attack: u8, decay: u8, sustain: u8, release: u8) -> None:
        assert _sid.envelope(self._index, attack, decay, sustain, release), "SID envelope fields must be 0..15"

    def enable(self) -> None:
        self.gate = True

    def disable(self) -> None:
        self.gate = False

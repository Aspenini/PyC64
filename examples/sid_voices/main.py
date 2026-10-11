from c64.video import screen
from c64.audio import sid

screen.clear()
sid.reset()
sid.set_clock(sid.PAL)
sid.volume(10)

lead = sid.voice(0)
lead.waveform = sid.TRIANGLE
lead.set_envelope(2, 4, 12, 5)
lead.frequency = 440
lead.gate = True

bass = sid.Voice(1)
bass.control = sid.PULSE | sid.SYNC
bass.pulse_width = 2048
bass.set_envelope(1, 2, 3, 4)
bass.frequency = 220
bass.gate = True

lead_word = lead.frequency_word
bass_word = bass.frequency_word
tuned = lead.frequency
print("SID voices:", lead_word, bass_word)
print("Gates:", lead.gate, bass.gate)

from c64.video import screen
import c64.sid as sid
import c64.audio as audio

screen.clear()
audio.reset()
volume_ok = audio.volume(10)
started = audio.note_on(0, 7493, audio.TRIANGLE, 2, 4, 12, 5)
pulse_ok = sid.pulse_width(1, 2048)
second = audio.note_on(1, 1000, audio.PULSE, 1, 2, 3, 4)
stopped = audio.note_off(1)
filter_ok = sid.filter(1023, 8, 1, sid.LOW_PASS)
print("SID ready:", started, stopped)

from c64 import music, irq
from c64.memory import peek

tune = music.load("theme.sid")
song_count = music.songs(tune)
default = music.default_song(tune)
started = music.play(tune)
assert started
while music.frames() < 48:
    pass
played = music.frames()
was_playing = music.playing()
stopped = music.stop()
after_stop = music.playing()
owned_after = irq.active()
restored_a = peek(0xDC0E)
restored_b = peek(0xDC0F)
assert stopped and not after_stop and not owned_after
assert restored_a == 1 and restored_b == 8
print("RSID interrupts:", played)

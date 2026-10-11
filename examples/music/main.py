from c64 import music

tune = music.load("theme.sid")
song_count = music.songs(tune)
default = music.default_song(tune)
started = music.play(tune)
if started:
    while music.frames() < 48:
        pass
played = music.frames()
was_playing = music.playing()
stopped = music.stop()
after_stop = music.playing()
print("Music frames:", played)

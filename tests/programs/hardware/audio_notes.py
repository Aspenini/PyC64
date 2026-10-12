from c64.audio import note_on as play, note_off as stop, volume, TRIANGLE
import c64.audio as audio
volume(10)
print(play(1, 7493, TRIANGLE, 2, 4, 12, 5), play(1, 1000, TRIANGLE, 0, 1, 2, 3), stop(1))
print(audio.note_on(2, 1000, audio.PULSE, 1, 2, 3, 4))
# expect: 0xD407=232
# expect: 0xD408=3
# expect: 0xD40B=16
# expect: 0xD40C=1
# expect: 0xD40D=35
# expect: 0xD418=10
# expect: 0xD410=0
# expect: 0xD411=8
# expect: 0xD412=65
#> TRUE TRUE TRUE
#> TRUE

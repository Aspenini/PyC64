from c64.video import screen
import c64.audio as audio
import c64.keyboard as keyboard

screen.clear()
audio.reset()
audio.volume(10)
# Raw SID frequency word; the resulting pitch depends on the PAL/NTSC clock.
started: bool = audio.note_on(0, 7493, audio.TRIANGLE, 2, 4, 12, 5)
print("Sound ready:", started)

# Give the envelope time to sound; keyboard input provides the exit.
keyboard.clear()
print("Press Q to stop")
while keyboard.read() != 81:
    pass
audio.note_off(0)
print("Done")

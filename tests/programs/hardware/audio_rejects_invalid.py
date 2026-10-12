import c64.audio as audio
print(audio.note_on(3, 1000, 16, 1, 2, 3, 4), audio.note_on(0, 1000, 0, 1, 2, 3, 4),
      audio.note_on(0, 1000, 48, 1, 2, 3, 4), audio.note_on(0, 1000, 16, 16, 2, 3, 4), audio.note_off(3))
# expect: 0xD400=0
# expect: 0xD404=0
#> FALSE FALSE FALSE FALSE FALSE

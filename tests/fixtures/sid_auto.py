from c64.audio import sid

sid.reset()
lead = sid.voice(0)
lead.frequency = 440
word = lead.frequency_word
clock = sid.clock_hz()
print("Auto tuning:", word)

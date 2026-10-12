import c64.sid as sid
from pyc64.types import u8
voice: u8 = 1
print(sid.frequency(voice, 65535), sid.pulse_width(voice, 4095), sid.envelope(voice, 2, 4, 12, 5),
      sid.control(voice, sid.PULSE | sid.SYNC), sid.gate(voice, True), sid.gate(voice, False))
# expect: 0xD407=255
# expect: 0xD408=255
# expect: 0xD409=255
# expect: 0xD40A=15
# expect: 0xD40B=66
# expect: 0xD40C=36
# expect: 0xD40D=197
# expect: 0xD400=0
# expect: 0xD40E=0
#> TRUE TRUE TRUE TRUE TRUE TRUE

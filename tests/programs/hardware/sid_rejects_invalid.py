# Invalid arguments return False and leave every register untouched.
import c64.sid as sid
print(sid.frequency(3, 65535), sid.pulse_width(0, 4096), sid.control(3, 255), sid.gate(3, True))
print(sid.envelope(0, 16, 0, 0, 0), sid.envelope(0, 0, 0, 0, 16), sid.volume(16))
print(sid.filter(2048, 0, 0, 0), sid.filter(0, 16, 0, 0), sid.filter(0, 0, 0, 1))
# expect: 0xD400=0
# expect: 0xD404=0
# expect: 0xD405=0
# expect: 0xD415=0
# expect: 0xD418=0
#> FALSE FALSE FALSE FALSE
#> FALSE FALSE FALSE
#> FALSE FALSE FALSE

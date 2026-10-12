# volume() and filter() share $D418 without disturbing each other's bits.
import c64.sid as sid
print(sid.volume(10), sid.filter(2047, 15, 7, sid.LOW_PASS | sid.VOICE3_OFF), sid.volume(3),
      sid.filter(0, 0, 8, sid.BAND_PASS), sid.volume(15))
# expect: 0xD415=0
# expect: 0xD416=0
# expect: 0xD417=8
# expect: 0xD418=47
#> TRUE TRUE TRUE TRUE TRUE

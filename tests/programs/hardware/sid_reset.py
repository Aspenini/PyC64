import c64.sid as sid
sid.control(0, 255)
sid.volume(15)
sid.filter(1000, 15, 15, sid.HIGH_PASS)
sid.reset()
sid.gate(0, True)
sid.volume(2)
# expect: 0xD404=1
# expect: 0xD418=2
# expect: 0xD417=0
# expect: 0xD416=0

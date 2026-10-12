from c64.video import screen as display
display.clear()
display.border = 6
display.background = 0
display.put_char(39, 24, "A")
print("hello\nworld")
# expect: 0xD020=6
# expect: 0xD021=0
# expect: 0x0400=32
# expect: 0x07E6=32
# expect: 0x07E7=1
# expect: 0xD800=1
# expect: 0xDBE7=1
#> HELLO
#> WORLD

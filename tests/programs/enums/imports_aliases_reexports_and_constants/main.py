from palette import Shade as Tone, DEFAULT, pick
import colors as original
current: Tone = pick(DEFAULT)
label = "BLUE"
blue = Tone[label]
print(current.name, blue.name, original.Color.RED.value)
#> GREEN BLUE 7

# requires: roms
# float() accepts Python's decimal spellings, including underscores and signs.
import math
texts = ["0", "134", "-2.5", " +.5 ", "1.", "1e2", "1E-2", "1_234.5", "\t-12.5\n", "1.2_5e+2", "65535"]
wanted = [0.0, 134.0, -2.5, 0.5, 1.0, 100.0, 0.01, 1234.5, -12.5, 125.0, 65535.0]
for index in range(11):
    print(math.fabs(float(texts[index]) - wanted[index]) < 1e-6)
#> TRUE
#> TRUE
#> TRUE
#> TRUE
#> TRUE
#> TRUE
#> TRUE
#> TRUE
#> TRUE
#> TRUE
#> TRUE

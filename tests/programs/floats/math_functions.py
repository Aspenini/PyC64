# requires: roms
import math as m
from math import sqrt as root, pi
from math import pow

def near(value: float, wanted: float) -> bool:
    return m.fabs(value - wanted) <= 2e-8 * m.fabs(wanted) + 1e-9

print(near(root(2), 1.4142135623730951), near(m.exp(1), m.e), near(m.log(2), 0.6931471805599453))
print(near(m.sin(pi / 6), 0.5), near(m.cos(pi / 3), 0.5), near(m.tan(pi / 4), 1.0), near(m.atan(1), pi / 4))
print(near(m.log2(8), 3.0), near(m.log10(100), 2.0), near(m.radians(180), pi), near(m.degrees(pi), 180.0))
print(near(m.e + m.tau, 9.001467135638631), near(pow(2, 3), 8.0))
print(m.sqrt(m.pow(3, 2) + m.pow(4, 2)), m.fabs(-2.5), m.pow(-3, 3))
#> TRUE TRUE TRUE
#> TRUE TRUE TRUE TRUE
#> TRUE TRUE TRUE TRUE
#> TRUE TRUE
#> 5.0 2.5 -27.0

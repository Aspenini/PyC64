# Target implementation of the supported standard Python math module.
from typing import Final
from pyc64._math import pow, sqrt, exp, log, sin, cos, tan, atan, fabs
from pyc64._math import floor, ceil, trunc

pi: Final[float] = 3.141592653589793
e: Final[float] = 2.718281828459045
tau: Final[float] = 6.283185307179586


def log2(x: float) -> float:
    return log(x) / log(2.0)


def log10(x: float) -> float:
    return log(x) / log(10.0)


def radians(x: float) -> float:
    return x * pi / 180.0


def degrees(x: float) -> float:
    return x * 180.0 / pi

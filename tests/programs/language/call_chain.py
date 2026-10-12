# A deep acyclic call chain fits the hardware stack in either order.
from pyc64.types import u8

def f19() -> u8:
    return f18() + 1
def f18() -> u8:
    return f17()
def f17() -> u8:
    return f16()
def f16() -> u8:
    return f15()
def f15() -> u8:
    return f14()
def f14() -> u8:
    return f13()
def f13() -> u8:
    return f12()
def f12() -> u8:
    return f11()
def f11() -> u8:
    return f10()
def f10() -> u8:
    return f9()
def f9() -> u8:
    return f8()
def f8() -> u8:
    return f7()
def f7() -> u8:
    return f6()
def f6() -> u8:
    return f5()
def f5() -> u8:
    return f4()
def f4() -> u8:
    return f3()
def f3() -> u8:
    return f2()
def f2() -> u8:
    return f1()
def f1() -> u8:
    return f0()
def f0() -> u8:
    return 7

print(f19())
#> 8

# A failing assert prints its message once and stops the program.
# fails: BRK guard
value = 3
calls = [0]

def message():
    calls[0] = calls[0] + 1
    return "assert failed"

assert value == 4, message()
value = 99
print("unreachable", value)
#> ASSERT FAILED

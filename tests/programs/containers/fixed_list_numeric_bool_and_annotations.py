values = [1, 500, 65535]
signed = [-128, 127, 250]
flags = [True, False]
labels: list[str] = ["red", "blue"]
empty = []
typed_empty: list[str] = []
values[-1] = 300
signed[-3] = -5
flags[1] = True
print(values[0], values[1], values[-1], signed[0], signed[2])
print(flags[0], flags[1], labels[-1], len(empty), len(typed_empty))
#> 1 500 300 -5 250
#> TRUE TRUE BLUE 0 0

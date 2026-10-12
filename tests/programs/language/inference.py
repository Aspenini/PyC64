# oracle: cpython
# Unannotated functions take their types from every call site.
def scale(value, factor):
    return value * factor

def label(flag):
    if flag:
        return "yes"
    return "no"

def nothing():
    return None

print(scale(6, 7), scale(300, 2), label(True), label(False))
#> 42 600 YES NO
words = ["red", "green", "blue"]
for word in words:
    print(word, len(word))
#> RED 3
#> GREEN 5
#> BLUE 4
nothing()

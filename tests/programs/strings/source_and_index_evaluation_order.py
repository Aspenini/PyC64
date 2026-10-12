# oracle: cpython
word = "Abc"
def source():
    print("source")
    return word
def index():
    print("index")
    return -1
print(source()[index()], len(source()))
def compare_source(value):
    print(value)
    return value
print(compare_source("a") < compare_source("b"))
#> SOURCE
#> INDEX
#> SOURCE
#> C 3
#> A
#> B
#> TRUE

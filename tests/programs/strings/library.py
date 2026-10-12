# oracle: cpython
from pyc64.strings import find, count, startswith, endswith, contains, isdigit, isspace

def check(text, pattern):
    print(find(text, pattern), count(text, pattern), startswith(text, pattern),
          endswith(text, pattern), contains(text, pattern))

check("", "")
check("", "x")
check("banana", "an")
check("aaaaa", "aa")
check("Abc", "abc")
check("Abc Abc", "Abc")
check("abc", "abcd")
for text in ["", "123", "12a", " ", "\t\n\r\v\f", " x"]:
    print(isdigit(text), isspace(text))
long_text = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab"
print(find(long_text, "b"), count(long_text, "aa"), startswith(long_text, "aaa"), endswith(long_text, "ab"))
#> 0 1 TRUE TRUE TRUE
#> -1 0 FALSE FALSE FALSE
#> 1 2 FALSE FALSE TRUE
#> 0 2 TRUE TRUE TRUE
#> -1 0 FALSE FALSE FALSE
#> 0 2 TRUE TRUE TRUE
#> -1 0 FALSE FALSE FALSE
#> FALSE FALSE
#> TRUE FALSE
#> FALSE FALSE
#> FALSE TRUE
#> FALSE TRUE
#> FALSE FALSE
#> 260 130 TRUE TRUE

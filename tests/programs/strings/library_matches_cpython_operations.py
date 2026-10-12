# oracle: cpython
from pyc64.strings import isdigit, isspace
for text in ["", "123", "12a", " ", "\t\n\r\v\f", " x"]:
    print(isdigit(text), isspace(text))
#> FALSE FALSE
#> TRUE FALSE
#> FALSE FALSE
#> FALSE TRUE
#> FALSE TRUE
#> FALSE FALSE

# oracle: cpython
# Equality compares contents, not addresses; ordering follows ASCII codes.
def compare(left, right):
    print(left == right, left != right, left < right, left <= right, left > right, left >= right)

compare("", "")
compare("", "a")
compare("a", "A")
compare("Z", "a")
compare("abc", "abcd")
compare("abcd", "abc")
compare("abc", "abd")
compare("abz", "aba")
compare("\n", "\r")
compare("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab")
#> TRUE FALSE FALSE TRUE FALSE TRUE
#> FALSE TRUE TRUE TRUE FALSE FALSE
#> FALSE TRUE FALSE FALSE TRUE TRUE
#> FALSE TRUE TRUE TRUE FALSE FALSE
#> FALSE TRUE TRUE TRUE FALSE FALSE
#> FALSE TRUE FALSE FALSE TRUE TRUE
#> FALSE TRUE TRUE TRUE FALSE FALSE
#> FALSE TRUE FALSE FALSE TRUE TRUE
#> FALSE TRUE TRUE TRUE FALSE FALSE
#> FALSE TRUE TRUE TRUE FALSE FALSE

# The prompt expression runs before input reads the line.
# pyc64test: --input "YES\rNO\r"
def prompt():
    print("preparing")
    return "Answer: "

answer = input(prompt())
other = input()
print(answer, other, answer == "YES", len(answer))
#> PREPARING
#> ANSWER: YES
#> NO
#> YES NO TRUE 3

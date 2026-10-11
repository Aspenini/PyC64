from pyc64.types import u8, u16, i16


def _matches_at(text: str, pattern: str, start: u16) -> bool:
    # Callers establish that the complete pattern fits at this position.
    for index in range(len(pattern)):
        if text[start + u16(index)] != pattern[index]:
            return False
    return True


def startswith(text: str, prefix: str) -> bool:
    if len(prefix) > len(text):
        return False
    return _matches_at(text, prefix, 0)


def endswith(text: str, suffix: str) -> bool:
    length: u16 = len(text)
    wanted: u16 = len(suffix)
    if wanted > length:
        return False
    return _matches_at(text, suffix, length - wanted)


def find(text: str, pattern: str) -> i16:
    length: u16 = len(text)
    wanted: u16 = len(pattern)
    if wanted > length:
        return -1
    limit: u16 = length - wanted
    for position in range(i16(limit) + 1):
        if _matches_at(text, pattern, u16(position)):
            return position
    return -1


def contains(text: str, pattern: str) -> bool:
    return find(text, pattern) >= 0


def count(text: str, pattern: str) -> u16:
    length: u16 = len(text)
    wanted: u16 = len(pattern)
    if wanted == 0:
        return length + 1
    if wanted > length:
        return 0
    position: u16 = 0
    matches: u16 = 0
    while position + wanted <= length:
        if _matches_at(text, pattern, position):
            matches += 1
            position += wanted
        else:
            position += 1
    return matches


def isdigit(text: str) -> bool:
    if not text:
        return False
    for character in text:
        if character < "0" or character > "9":
            return False
    return True


def isspace(text: str) -> bool:
    if not text:
        return False
    for character in text:
        value: u8 = ord(character)
        if not (value >= 9 and value <= 13) and not (value >= 28 and value <= 32):
            return False
    return True

# Draw prepared monochrome tile maps with a linked character set in VIC bank 0.
# MAP starts with little-endian u16 width/height, then row-major glyph indices.
# Drawing uses screen $0400 and color RAM $D800; callers keep VIC bank 0.
# Invalid input returns False before changing hardware.
from pyc64.types import u8, u16
from c64.memory import peek, poke
from c64.assets import address


def _valid(data: bytes) -> bool:
    if len(data) < 5:
        return False
    w: u16 = u16(data[0]) | (u16(data[1]) << 8)
    h: u16 = u16(data[2]) | (u16(data[3]) << 8)
    if w == 0 or h == 0 or w > 256 or h > 256:
        return False
    cells: u16 = len(data) - 4
    # Division avoids overflow for a malformed 256x256 header.
    return cells // w == h and cells % w == 0


def width(data: bytes) -> u16:
    if not _valid(data):
        return 0
    return u16(data[0]) | (u16(data[1]) << 8)


def height(data: bytes) -> u16:
    if not _valid(data):
        return 0
    return u16(data[2]) | (u16(data[3]) << 8)


def _font_address(font: bytes) -> u16:
    if len(font) < 8 or len(font) > 2048 or len(font) % 8 != 0:
        return 0
    location: u16 = address(font)
    if location == 0 or location >= 16384 or location % 2048 != 0:
        return 0
    if location >= 4096 and location < 8192:
        return 0
    if location + len(font) > 16384:
        return 0
    return location


def _select_font(location: u16) -> None:
    # Select screen $0400 and the font. Caller keeps standard text mode/bank 0.
    poke(0xD018, (peek(0xD018) & 1) | 16 | u8(location // 1024))


def set_charset(font: bytes) -> bool:
    location: u16 = _font_address(font)
    if location == 0:
        return False
    _select_font(location)
    return True


def draw_region(data: bytes, font: bytes, source_x: u16, source_y: u16,
                columns: u8, rows: u8, x: u8, y: u8, color: u8) -> bool:
    if not _valid(data):
        return False
    w: u16 = u16(data[0]) | (u16(data[1]) << 8)
    h: u16 = u16(data[2]) | (u16(data[3]) << 8)
    if columns == 0 or rows == 0 or columns > 40 or rows > 25:
        return False
    if source_x < 0 or source_y < 0 or source_x >= w or source_y >= h:
        return False
    if u16(columns) > w - source_x or u16(rows) > h - source_y:
        return False
    if x < 0 or y < 0 or color < 0 or x >= 40 or y >= 25 or color > 15:
        return False
    if columns > 40 - x or rows > 25 - y:
        return False
    location: u16 = _font_address(font)
    if location == 0:
        return False
    glyphs: u16 = len(font) // 8
    # Validate every selected index before changing the mode, screen or colors.
    for row in range(rows):
        start: u16 = 4 + (source_y + u16(row)) * w + source_x
        for column in range(columns):
            if u16(data[start + u16(column)]) >= glyphs:
                return False
    _select_font(location)
    for row in range(rows):
        start: u16 = 4 + (source_y + u16(row)) * w + source_x
        target: u16 = (u16(y) + u16(row)) * 40 + u16(x)
        for column in range(columns):
            offset: u16 = target + u16(column)
            poke(0x0400 + offset, data[start + u16(column)])
            poke(0xD800 + offset, color)
    return True


def draw(data: bytes, font: bytes, x: u8, y: u8, color: u8) -> bool:
    w: u16 = width(data)
    h: u16 = height(data)
    if w == 0 or h == 0 or w > 40 or h > 25:
        return False
    return draw_region(data, font, 0, 0, u8(w), u8(h), x, y, color)

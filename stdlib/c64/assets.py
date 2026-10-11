"""PNG/SPR/CHR/MAP/HBM/SCR conversion on desktop; immutable bytes results.

CPython PNG conversion needs Pillow. The native compiler has its own bundled
PNG decoder. Native addresses require a C64 adapter under CPython.
"""
from pathlib import Path
from pyc64.assets import load
from pyc64.types import u16

_DEFAULT_PALETTE = "000000,555555,aaaaaa,ffffff"
_BITMAP_PALETTE = "000000,ffffff,68372b,70a4b2,6f3d86,588d43,352879,b8c76f,6f4f25,433900,9a6759,444444,6c6c6c,9ad284,6c5eb5,959595"


def _convert(path: str, mode: int, palette: str = _DEFAULT_PALETTE) -> bytes:
    suffix = Path(path).suffix.lower()
    if mode == 3 or mode >= 7:
        colors = palette.split(",")
        if len(colors) != (16 if mode >= 7 else 4) or any(len(color) != 6 or any(ch not in "0123456789abcdefABCDEF" for ch in color) for color in colors):
            raise ValueError("palette requires sixteen colors for bitmaps or four for sprites, as comma-separated six-digit RGB")
    if (mode == 7 and suffix == ".hbm") or (mode == 8 and suffix == ".scr"):
        data = load(path)
        if len(data) != (8000 if mode == 7 else 1000):
            raise ValueError("HBM requires 8000 bytes; SCR requires 1000 bytes")
        return data
    if mode == 6 and suffix == ".map":
        data = load(path)
        width = int.from_bytes(data[:2], "little")
        height = int.from_bytes(data[2:4], "little")
        if not (1 <= width <= 256 and 1 <= height <= 256 and len(data) == 4 + width * height):
            raise ValueError("MAP requires 1..256 columns/rows and exactly width * height tile indices")
        return data
    if (mode in (2, 3) and suffix == ".spr") or (mode in (4, 5) and suffix == ".chr"):
        data = load(path)
        stride = 8 if mode in (4, 5) else 64
        if not data or len(data) % stride or (mode in (4, 5) and len(data) > 2048):
            raise ValueError("prepacked asset requires complete sprite/glyph records")
        return data
    colors = palette.split(",")
    if len(colors) != (16 if mode >= 7 else 4) or any(len(color) != 6 or any(ch not in "0123456789abcdefABCDEF" for ch in color) for color in colors):
        raise ValueError("palette requires four comma-separated six-digit RGB colors")
    rgb = [tuple(int(color[j:j + 2], 16) for j in (0, 2, 4)) for color in colors]
    from PIL import Image
    with Image.open(path) as source:
        if source.format != "PNG" or source.width > 2048 or source.height > 2048:
            raise ValueError("PNG dimensions limited to 2048")
        if source.mode in ("I", "I;16", "I;16B", "I;16L"):
            # PNG 16-bit grayscale keeps all sample bits in Pillow. Match the
            # native decoder's high-byte conversion before classifying pixels.
            image = Image.new("RGBA", source.size)
            transparent = source.info.get("transparency")
            image.putdata([(value >> 8, value >> 8, value >> 8,
                            0 if value == transparent else 255)
                           for value in source.getdata()])
        else:
            image = source.convert("RGBA")
    tw, th, stride = (12, 21, 64) if mode == 3 else (8, 8, 8) if mode >= 4 else (24, 21, 64)
    width, height = image.size
    if mode >= 7:
        if (width, height) != (320, 200):
            raise ValueError("hires bitmap PNG must be 320x200")
        data = bytearray()
        def distance(color, index):
            return sum((color[k] - rgb[index][k]) ** 2 for k in range(3))
        for ty in range(0, 200, 8):
            for tx in range(0, 320, 8):
                pixels = [image.getpixel((tx + x, ty + y)) for y in range(8) for x in range(8)]
                pixels = [color[:3] if color[3] >= 128 else rgb[0] for color in pixels]
                counts = [0] * 16
                for color in pixels:
                    counts[min(range(16), key=lambda j: distance(color, j))] += 1
                background = max(range(16), key=lambda j: counts[j])
                foreground = max((j for j in range(16) if j != background), key=lambda j: counts[j])
                if mode == 8:
                    data.append(foreground * 16 + background)
                else:
                    for y in range(8):
                        bits = 0
                        for x in range(8):
                            color = pixels[y * 8 + x]
                            if distance(color, foreground) < distance(color, background):
                                bits |= 1 << (7 - x)
                        data.append(bits)
        return bytes(data)
    if width % tw or height % th:
        raise ValueError(f"PNG dimensions must be multiples of {tw}x{th}")
    if mode in (5, 6):
        if mode == 6 and 4 + width // 8 * (height // 8) > 32764:
            raise ValueError("tile map exceeds the 32764-byte payload limit")
        glyphs = bytearray()
        indices = bytearray()
        lookup = {}
        for ty in range(0, height, 8):
            for tx in range(0, width, 8):
                tile = bytearray(8)
                for y in range(8):
                    for x in range(8):
                        color = image.getpixel((tx + x, ty + y))
                        if color[3] >= 128 and any(color[:3]):
                            tile[y] |= 1 << (7 - x)
                key = bytes(tile)
                if key not in lookup:
                    if len(lookup) == 256:
                        raise ValueError("tile image exceeds 256 distinct glyphs")
                    lookup[key] = len(lookup)
                    glyphs.extend(tile)
                indices.append(lookup[key])
        if mode == 5:
            return bytes(glyphs)
        return (width // 8).to_bytes(2, "little") + (height // 8).to_bytes(2, "little") + bytes(indices)
    count = width // tw * (height // th) * stride
    if count > 32764 or (mode == 4 and count > 2048):
        raise ValueError("converted asset exceeds sprite/glyph payload limit")
    data = bytearray(count)
    offset = 0
    for ty in range(0, height, th):
        for tx in range(0, width, tw):
            for y in range(th):
                for x in range(tw):
                    color = image.getpixel((tx + x, ty + y))
                    value = 0
                    if color[3] >= 128:
                        if mode == 3:
                            value = min(range(4), key=lambda j: sum((color[k] - rgb[j][k]) ** 2 for k in range(3)))
                        else:
                            value = int(any(color[:3]))
                    bits = 2 if mode == 3 else 1
                    position = x * bits
                    data[offset + y * (tw * bits // 8) + position // 8] |= value << (8 - bits - position % 8)
            offset += stride
    return bytes(data)


def sprite(path: str) -> bytes:
    return _convert(path, 2)


def sprite_multicolor(path: str, palette: str = _DEFAULT_PALETTE) -> bytes:
    return _convert(path, 3, palette)


def charset(path: str) -> bytes:
    return _convert(path, 4)


def tileset(path: str) -> bytes:
    return _convert(path, 5)


def tilemap(path: str) -> bytes:
    return _convert(path, 6)


def bitmap(path: str, palette: str = _BITMAP_PALETTE) -> bytes:
    return _convert(path, 7, palette)


def bitmap_screen(path: str, palette: str = _BITMAP_PALETTE) -> bytes:
    return _convert(path, 8, palette)


def address(data: bytes) -> u16:
    raise RuntimeError("native asset addresses need a C64 adapter under CPython")


def sid(path: str) -> bytes:
    """Load the single-SID PSID/RSID profile used by c64.music."""
    data = load(path)
    rsid = data[:4] == b"RSID"
    if len(data) < 118 or data[:4] not in (b"PSID", b"RSID"):
        raise ValueError("SID requires a complete PSID/RSID header")
    def word(offset):
        return int.from_bytes(data[offset:offset + 2], "big")
    version = word(4)
    if not (2 if rsid else 1) <= version <= 4:
        raise ValueError("RSID version must be 2..4" if rsid else "PSID version must be 1..4")
    if rsid and (word(8) or word(12) or any(data[18:22])):
        raise ValueError("RSID reserved load/play/speed fields must be zero")
    header = 118 if version == 1 else 124
    if len(data) <= header or word(6) != header:
        raise ValueError("PSID has an invalid data offset or empty payload")
    offset, location = header, word(8)
    if not location:
        if len(data) < header + 3:
            raise ValueError("PSID payload is missing its load address")
        location = int.from_bytes(data[header:header + 2], "little")
        offset += 2
    if not 1 <= word(14) <= 256 or not 1 <= word(16) <= word(14):
        raise ValueError("PSID songs/start song must be within 1..256")
    if version >= 2:
        flags = word(118)
        if rsid and any(data[120:122]):
            raise ValueError("RSID library playback requires a clean image with zero relocation fields")
        if rsid and flags & 2:
            raise ValueError("RSID BASIC launches need a standalone execution environment")
        if flags & ~0x3ff or flags & 3:
            raise ValueError("PSID MUS/PlaySID-specific or reserved flags are unsupported")
        if data[122] or data[123] or flags & 0x3c0:
            raise ValueError("PSID playback requires one SID chip at $D400")
    end = location + len(data) - offset
    if not ((0x0810 <= location < end <= 0xa000) or (0xc000 <= location < end <= 0xd000)):
        raise ValueError("PSID payload must fit RAM $0810..$9FFF or $C000..$CFFF")
    if not location <= (word(10) or location) < end or (not rsid and not location <= word(12) < end):
        raise ValueError("RSID requires a returning machine-code init inside its payload" if rsid else
                         "PSID requires init/play RTS routines inside its payload")
    return data

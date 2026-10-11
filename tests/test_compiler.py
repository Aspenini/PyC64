"""Compile source and execute the actual PRG with an independent NMOS 6502 emulator."""
from pathlib import Path
import ast
import math
import os
import random
import re
import shutil
import subprocess
import struct
import sys
import tempfile
import tomllib
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build' / 'test-deps'))
from py65.devices.mpu6502 import MPU
from hardware import REUMemory, IRQMemory, CIAMemory, SidLaunchMemory

if len(sys.argv) < 2:
    raise SystemExit('Usage: python tests/test_compiler.py COMPILER (or run xmake test)')
COMPILER = Path(sys.argv.pop(1)).resolve()

# No ROM images are bundled. A standard VICE KERNAL beside BASIC is sufficient;
# an explicit setting supports other ROM filenames and installations.
KERNAL_ROM = os.environ.get('PYC64_KERNAL_ROM')
if not KERNAL_ROM and os.environ.get('PYC64_BASIC_ROM'):
    candidate = Path(os.environ['PYC64_BASIC_ROM']).with_name('kernal-901227-03.bin')
    if candidate.is_file():
        KERNAL_ROM = str(candidate)
MATH_ROMS = bool(os.environ.get('PYC64_BASIC_ROM') and KERNAL_ROM)


def library_bindings(text):
    """Generate explicit imports for compiler fixtures, never hidden compiler globals."""
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return text
    names = {node.id for node in ast.walk(tree) if isinstance(node, ast.Name)}
    bound = {alias.asname or alias.name.split('.')[0]
             for node in ast.walk(tree) if isinstance(node, (ast.Import, ast.ImportFrom))
             for alias in node.names}
    prefix = []
    for module, exports in [('pyc64.types', {'u8', 'i8', 'u16', 'i16', 'char', 'array'}),
                            ('typing', {'Final'}), ('c64.memory', {'peek', 'poke'})]:
        needed = sorted((names & exports) - bound)
        if needed:
            prefix.append(f'from {module} import {", ".join(needed)}\n')
    return ''.join(prefix) + text


class SIDMemory(list):
    """Model the SID's write-only register boundary independently of library code."""
    def __init__(self, memory):
        super().__init__(memory)
        self.registers = {}
        self.writes = []
        self.reads = []

    def __getitem__(self, key):
        if isinstance(key, int) and 0xD400 <= key <= 0xD418:
            self.reads.append(key)
            return 0  # These registers cannot be read back like ordinary RAM.
        return super().__getitem__(key)

    def __setitem__(self, key, value):
        if isinstance(key, int) and 0xD400 <= key <= 0xD418:
            self.registers[key] = value
            self.writes.append((key, value))
        super().__setitem__(key, value)


class DiskKernal:
    """Small serial-file adapter; full ROM/1541 behavior is checked in VICE."""
    ROUTINES = {0xFFBD, 0xFFBA, 0xFFC0, 0xFFC3, 0xFFC6, 0xFFC9,
                0xFFCF, 0xFFD2, 0xFFB7, 0xFFCC}

    def __init__(self):
        self.files = {}
        self.channels = {}
        self.input = self.output = None
        self.name = b''
        self.lfs = (0, 0, 0)
        self.pending = None
        self.commands = []
        self.command_buffer = bytearray()
        self.responses = []
        self.dos_record = b'73,CBM DOS V2.6 1541,00,00\r'
        self.open_error = 0
        self.flush_status = 0
        self.read_status = 0
        self.read_error_after = None
        self.write_status = 0
        self.write_error_after = None
        self.write_count = 0
        self.read_count = 0
        self.opened = []
        self.closed = []

    def handle(self, program):
        cpu = program.cpu
        routine, a, x, y = cpu.pc, cpu.a, cpu.x, cpu.y
        if routine not in self.ROUTINES or (routine == 0xFFD2 and self.output is None):
            return False
        cpu.a, cpu.x, cpu.y = 0xAB, 0xCD, 0xEF
        # CHRIN/CHROUT/CLOSE carry is deliberately unhelpful; use READST.
        cpu.p |= cpu.CARRY
        if routine == 0xFFBD:
            start = x | (y << 8)
            self.name = bytes(cpu.memory[start:start + a])
        elif routine == 0xFFBA:
            self.lfs = (a, x, y)
        elif routine == 0xFFC0:
            logical, device, secondary = self.lfs
            self.opened.append((*self.lfs, self.name))
            error = self.open_error or (2 if logical in self.channels else 0)
            if device != 8:
                error = 5
                cpu.memory[0x90] = 128
            if error:
                cpu.a = error
            else:
                parts = self.name.split(b',')
                name, writing = parts[0], parts[-1] == b'W'
                created = writing and name not in self.files and secondary != 15
                if secondary != 15:
                    if writing and name in self.files:
                        self.dos_record = b'63,FILE EXISTS,00,00\r'
                    elif writing:
                        self.files[name] = bytearray()
                        self.dos_record = b'00,OK,00,00\r'
                    elif name not in self.files:
                        self.dos_record = b'62,FILE NOT FOUND,00,00\r'
                    else:
                        self.dos_record = b'00,OK,00,00\r'
                self.channels[logical] = [name, writing, 0, secondary, b'', created]
                cpu.p &= ~cpu.CARRY
        elif routine == 0xFFC3:
            self.closed.append(a)
            if a in self.channels and self.channels[a][3] == 15:
                for channel in self.channels.values():
                    if channel[5] and not self.files[channel[0]]:
                        self.files[channel[0]].append(13)
                self.channels.clear()
            if a in self.channels:
                channel = self.channels[a]
                if channel[5] and not self.files[channel[0]]:
                    self.files[channel[0]].append(13)
            self.channels.pop(a, None)
        elif routine in (0xFFC6, 0xFFC9):
            if x not in self.channels:
                cpu.a = 3
            else:
                if routine == 0xFFC6:
                    self.input = x
                else:
                    self.output = x
                cpu.p &= ~cpu.CARRY
        elif routine == 0xFFCF:
            self.read_count += 1
            channel = self.channels[self.input]
            if channel[3] == 15:
                if channel[2] == 0:
                    channel[4] = self.responses.pop(0) if self.responses else self.dos_record
                data = channel[4]
            else:
                data = self.files.get(channel[0], b'')
            position = channel[2]
            cpu.a = data[position] if position < len(data) else 0
            channel[2] += 1
            error_status = self.read_status if self.read_error_after is None or self.read_count >= self.read_error_after else 0
            cpu.memory[0x90] = error_status or (64 if channel[2] >= len(data) else 0)
            if channel[3] == 15 and channel[2] >= len(data):
                channel[2] = 0
                self.dos_record = b'00,OK,00,00\r'
        elif routine == 0xFFD2:
            if self.pending:
                self.flush_pending()
            self.pending = (self.output, a)
            self.write_count += 1
            if self.write_error_after is not None and self.write_count >= self.write_error_after:
                cpu.memory[0x90] = self.write_status
        elif routine == 0xFFB7:
            cpu.a = cpu.memory[0x90]
        elif routine == 0xFFCC:
            # CLRCHN can change status after sending the buffered last byte.
            cpu.memory[0x90] = self.flush_status if self.pending else 0
            if self.pending:
                self.flush_pending()
                if self.output is not None and self.channels[self.output][3] == 15:
                    command = bytes(self.command_buffer)
                    self.commands.append(command)
                    if command.startswith(b'R0:') and b'=' in command:
                        new, old = command[3:].split(b'=', 1)
                        if old not in self.files:
                            self.dos_record = b'62,FILE NOT FOUND,00,00\r'
                        elif new in self.files:
                            self.dos_record = b'63,FILE EXISTS,00,00\r'
                        else:
                            self.files[new] = self.files.pop(old)
                            self.dos_record = b'00,OK,00,00\r'
                    self.command_buffer.clear()
            self.input = self.output = None
        cpu.pc = (cpu.stPopWord() + 1) & 0xFFFF
        return True

    def flush_pending(self):
        logical, value = self.pending
        channel = self.channels[logical]
        if channel[3] == 15:
            self.command_buffer.append(value)
        else:
            self.files[channel[0]].append(value)
        self.pending = None


class Program:
    def __init__(self, binary, symbols):
        self.cpu = MPU()
        self.symbols = symbols
        self.output = bytearray()
        self.clobber_kernal = False
        self.keyboard = None
        self.disk = None
        self.irq = None
        self.cpu.memory[0x0801:0x0801 + len(binary) - 2] = binary[2:]
        self.cpu.memory[0xDC00] = self.cpu.memory[0xDC01] = 255
        self.cpu.memory[0x0400:0x07E8] = [255] * 1000
        self.cpu.memory[0xFB:0xFF] = [11, 22, 33, 44]
        self.restart()

    def restart(self):
        self.cpu.pc = 0x080D
        self.cpu.sp = 0xF0
        # Simulate the caller's JSR return address; execution ends at $0200.
        self.cpu.stPushWord(0x01FF)

    def run(self, limit=1_000_000, stop=None):
        for _ in range(limit):
            if self.cpu.pc == 0x0200:
                return
            if stop and stop(self):
                return
            if self.irq and self.irq.handle(self):
                continue
            if self.disk and self.disk.handle(self):
                continue
            if self.cpu.pc == 0xFFD2:
                self.output.append(self.cpu.a)
                if self.clobber_kernal:
                    self.cpu.a, self.cpu.x, self.cpu.y = 0xAB, 0xCD, 0xEF
                self.cpu.pc = (self.cpu.stPopWord() + 1) & 0xFFFF
                continue
            if self.cpu.pc == 0xFFE4:
                if self.keyboard is not None:
                    self.cpu.a = self.keyboard.pop(0) if self.keyboard else 0
                    if self.clobber_kernal:
                        self.cpu.x, self.cpu.y = 0xCD, 0xEF
                    self.cpu.pc = (self.cpu.stPopWord() + 1) & 0xFFFF
                    continue
                count = self.cpu.memory[0xC6]
                self.cpu.a = self.cpu.memory[0x0277] if count else 0
                if count:
                    self.cpu.memory[0x0277:0x0277 + count - 1] = self.cpu.memory[0x0278:0x0277 + count]
                    self.cpu.memory[0xC6] = count - 1
                self.cpu.pc = (self.cpu.stPopWord() + 1) & 0xFFFF
                continue
            if self.cpu.memory[self.cpu.pc] == 0:
                raise RuntimeError('runtime BRK guard')
            self.cpu.step()
        raise RuntimeError('instruction limit exceeded')

    def value(self, name):
        address, size, type_name = self.symbols[name]
        if type_name == 'float':
            data = self.cpu.memory[address:address + 5]
            if not data[0]:
                return 0.0
            fraction = ((data[1] & 127) << 24) | (data[2] << 16) | (data[3] << 8) | data[4]
            return (-1 if data[1] & 128 else 1) * (1 + fraction / 2147483648) * 2.0 ** (data[0] - 129)
        value = int.from_bytes(bytes(self.cpu.memory[address:address + size]), 'little')
        if type_name.startswith('i') and value & (1 << (size * 8 - 1)):
            value -= 1 << (size * 8)
        return value


class CompilerTests(unittest.TestCase):
    def asset_png(self, name, width, height, rows, color=6, depth=8, palette=None, transparency=None, interlace=0):
        def chunk(kind, payload):
            return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
        data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, color, 0, 0, interlace))
        if palette is not None:
            data += chunk(b'PLTE', palette)
        if transparency is not None:
            data += chunk(b'tRNS', transparency)
        data += chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
        path = self.dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_asset_binary_imports_constants_functions_and_restart(self):
        blob = bytes(range(256))
        (self.dir / 'data.bin').write_bytes(blob)
        (self.dir / 'empty.bin').write_bytes(b'')
        self.module('exports.py', 'from pyc64.assets import load as read\n')
        p = self.compile('''from exports import read
from pyc64 import assets
from c64.assets import address
PATH: Final[str] = "data.bin"
DATA: Final[bytes] = read(PATH)
def saved() -> bytes:
    return assets.load("data.bin")
first = saved()
empty = assets.load("empty.bin")
assert first == DATA and len(first) == 256 and not empty
position = address(first)
total: u16 = 0
for value in first:
    total += value
last = first[-1]
print(total, last)
''')
        mapping = (self.dir / 'main.map').read_text()
        for _ in range(2):
            p.run()
            self.assertEqual(p.output, b'32640 255\r')
            self.assertEqual(bytes(p.cpu.memory[p.value('position'):p.value('position') + 256]), blob)
            p.restart()
            p.output.clear()
        self.assertIn('Assets: 256 payload bytes', self.invoke('from pyc64.assets import load\ndata = load("data.bin")\n')[0].stdout)
        self.assertEqual(len(re.findall(r'^asset ', mapping, re.M)), 2)
        self.assertNotIn('runtime immutable_bytes ', mapping)
        (self.dir / 'sub').mkdir()
        (self.dir / 'sub/local.bin').write_bytes(b'local')
        self.module('sub/holder.py', 'from pyc64.assets import load\ndef get():\n    return load("local.bin")\n')
        p = self.compile('from sub.holder import get\nprint(get())\n')
        p.run()
        self.assertEqual(p.output, b"B'LOCAL'\r")
        script = f'import sys\nsys.path.insert(0, {str(ROOT / "stdlib")!r})\nfrom pyc64.assets import load\nassert load("data.bin") == bytes(range(256))\n'
        result = subprocess.run([sys.executable, '-c', script], cwd=self.dir, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_asset_example_native_addresses_glyphs_and_restart(self):
        root = ROOT / 'examples/assets'
        for path in (root / 'assets').iterdir():
            shutil.copyfile(path, self.dir / path.name)
        p = self.compile((root / 'main.py').read_text())
        mapping = (self.dir / 'main.map').read_text()
        self.assertEqual(len(re.findall(r'^asset ', mapping, re.M)), 4)
        for _ in range(2):
            p.run()
            self.assertEqual(p.output, b'ASSETS: 128 2048\rLEVEL: 32640\r')
            self.assertEqual(p.value('frame_size'), 128)
            self.assertEqual(p.value('font_size'), 2048)
            self.assertEqual(p.value('checksum'), 32640)
            self.assertEqual(p.value('pointer_ok'), 1)
            self.assertEqual(p.value('font_selected'), 1)
            self.assertEqual(p.cpu.memory[0xD015] & 7, 7)
            self.assertEqual(p.cpu.memory[0xD01C] & 7, 4)
            self.assertEqual(p.cpu.memory[0x07F9], p.cpu.memory[0x07F8] + 1)
            p.restart()
            p.output.clear()

    def test_asset_sprite_sheet_packing_and_vic_placement(self):
        rows = []
        expected = bytearray(4 * 64)
        for y in range(42):
            row = bytearray([0])
            for x in range(48):
                tile = y // 21 * 2 + x // 24
                value = (x % 24 + y % 21 + tile) % 3 == 0
                alpha = 127 if tile == 3 and x % 24 == 0 else 255
                row.extend((255, 255, 255, alpha) if value else (0, 0, 0, 255))
                if value and alpha >= 128:
                    expected[tile * 64 + y % 21 * 3 + x % 24 // 8] |= 1 << (7 - x % 8)
            rows.append(bytes(row))
        path = self.asset_png('sheet.png', 48, 42, rows)
        result = self.cli('asset', 'sprite', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.spr').read_bytes(), expected)
        p = self.compile('''from c64 import assets
from c64.sprite import Sprite
image = assets.sprite("sheet.png")
prepacked = assets.sprite("sheet.spr")
assert image == prepacked
location = assets.address(image)
assert location % 64 == 0
player = Sprite(3)
player.set_data(location + 64)
checksum: u16 = 0
for value in image:
    checksum += value
print(len(image), checksum)
''')
        for _ in range(2):
            p.run()
            location = p.value('location')
            self.assertEqual(location % 64, 0)
            self.assertTrue(location >= 0x2000 or location + 256 <= 0x1000)
            self.assertEqual(bytes(p.cpu.memory[location:location + 256]), expected)
            self.assertEqual(p.cpu.memory[0x07FB], (location + 64) // 64)
            self.assertEqual(p.value('checksum'), sum(expected))
            p.restart()
            p.output.clear()

    def test_asset_multicolor_palette_indices_alpha_and_sheets(self):
        palette = '000000,ff0000,00ff00,0000ff'
        colors = [(0, 0, 0, 255), (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)]
        rows = [bytes([0]) + bytes(channel for x in range(24) for channel in colors[x % 4]) for _ in range(21)]
        path = self.asset_png('color.png', 24, 21, rows)
        expected = (b'\x1b' * 63 + b'\0') * 2
        result = self.cli('asset', 'sprite-multicolor', path, '--palette', palette)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.spr').read_bytes(), expected)
        p = self.compile(f'from c64.assets import sprite_multicolor, address\nimage = sprite_multicolor("color.png", "{palette}")\nlocation = address(image)\n')
        p.run()
        self.assertEqual(bytes(p.cpu.memory[p.value('location'):p.value('location') + 128]), expected)
        gray = bytes([0]) + bytes([0, 85, 170, 255] * 3)
        path = self.asset_png('gray.png', 12, 21, [gray] * 21, color=0)
        result = self.cli('asset', 'sprite-multicolor', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.spr').read_bytes(), b'\x1b' * 63 + b'\0')
        indexed = bytes([0]) + bytes([0, 1, 2, 3] * 3)
        path = self.asset_png('indexed.png', 12, 21, [indexed] * 21, color=3,
                              palette=bytes([0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255]), transparency=bytes([255, 127, 255, 255]))
        result = self.cli('asset', 'sprite-multicolor', path, '--palette', palette)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.spr').read_bytes(), b'\x0b' * 63 + b'\0')

    def test_asset_charset_tile_order_2048_alignment_and_prepacked(self):
        rows = [bytes([0]) + bytes(255 if x // 8 == y // 8 else 0 for x in range(16)) for y in range(16)]
        path = self.asset_png('chars.png', 16, 16, rows, color=0)
        expected = b'\xff' * 8 + b'\0' * 16 + b'\xff' * 8
        result = self.cli('asset', 'charset', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.chr').read_bytes(), expected)
        p = self.compile('from c64.assets import charset, address\nfont = charset("chars.png")\n'
                         'other = charset("chars.chr")\nassert font == other\nlocation = address(font)\nprint(len(font))\n')
        p.run()
        location = p.value('location')
        self.assertEqual(location % 2048, 0)
        self.assertGreaterEqual(location, 0x2000)
        self.assertEqual(bytes(p.cpu.memory[location:location + 32]), expected)
        self.assertRegex((self.dir / 'main.map').read_text(), r'asset \$[0-9A-F]+ 32 align=2048 mode=4')

    def test_asset_png_filters_bit_depths_and_compression(self):
        original = bytes([255, 0, 255] * 8)
        for filter_kind in range(5):
            filtered = bytearray()
            for j, value in enumerate(original):
                left = original[j - 1] if j else 0
                above = value
                diagonal = original[j - 1] if j else 0
                if filter_kind == 4:
                    estimate = left + above - diagonal
                    distances = (abs(estimate - left), abs(estimate - above), abs(estimate - diagonal))
                    predictor = (left, above, diagonal)[distances.index(min(distances))]
                else:
                    predictor = (0, left, above, (left + above) // 2)[filter_kind]
                filtered.append((value - predictor) & 255)
            path = self.asset_png(f'filter{filter_kind}.png', 24, 21,
                                  [b'\0' + original] + [bytes([filter_kind]) + bytes(filtered)] * 20, color=0)
            result = self.cli('asset', 'sprite', path)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(path.with_suffix('.spr').read_bytes(), b'\xb6\xdb\x6d' * 21 + b'\0')
        path = self.asset_png('onebit.png', 24, 21, [b'\0\xaa\x55\xf0'] * 21, color=0, depth=1)
        result = self.cli('asset', 'sprite', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.spr').read_bytes(), b'\xaa\x55\xf0' * 21 + b'\0')
        path = self.asset_png('sixteen.png', 24, 21, [b'\0' + b'\xff\xff\0\0' * 12] * 21, color=0, depth=16)
        result = self.cli('asset', 'sprite', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.spr').read_bytes(), b'\xaa' * 63 + b'\0')
        passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
                  (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
        scanlines = [b'\0' + bytes(255 if x % 2 == 0 else 0 for x in range(sx, 24, dx))
                     for sx, sy, dx, dy in passes for y in range(sy, 21, dy)]
        path = self.asset_png('interlaced.png', 24, 21, scanlines, color=0, interlace=1)
        result = self.cli('asset', 'sprite', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.spr').read_bytes(), b'\xaa' * 63 + b'\0')

    def test_asset_bounded_arenas_maximum_payload_and_large_charsets(self):
        maximum = bytes(index % 256 for index in range(32764))
        (self.dir / 'max.bin').write_bytes(maximum)
        p = self.compile('from pyc64.assets import load\nfrom c64.assets import address\n'
                         'data = load("max.bin")\nagain = load("max.bin")\n'
                         'location = address(data)\nassert data == again\nsize = len(data)\n')
        p.run(limit=3_000_000)
        self.assertEqual(p.value('size'), 32764)
        self.assertEqual(bytes(p.cpu.memory[p.value('location'):p.value('location') + 32764]), maximum)
        (self.dir / 'one.bin').write_bytes(b'other')
        self.reject('from pyc64.assets import load\na = load("max.bin")\nb = load("one.bin")\n', 'bytes literal arena exhausted')
        for index in range(65):
            (self.dir / f'{index}.bin').write_bytes(b'')
        source = 'from pyc64.assets import load\n' + ''.join(f'a{index} = load("{index}.bin")\n' for index in range(65))
        self.reject(source, 'asset arena exhausted (limit 64)')
        path = self.asset_png('fullfont.png', 128, 128, [b'\0' + b'\xff' * 128] * 128, color=0)
        result = self.cli('asset', 'charset', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.chr').read_bytes(), b'\xff' * 2048)
        p = self.compile('from c64.assets import charset, address\nfont = charset("fullfont.png")\n'
                         'location = address(font)\nsize = len(font)\n')
        p.run()
        self.assertEqual(p.value('size'), 2048)
        self.assertEqual(bytes(p.cpu.memory[p.value('location'):p.value('location') + 2048]), b'\xff' * 2048)
        path = self.asset_png('toomany.png', 128, 136, [b'\0' + b'\0' * 128] * 136, color=0)
        self.assertIn('256 glyphs', self.cli('asset', 'charset', path).stderr)
        path = self.asset_png('brokenstream.png', 24, 21, [b'bad'], color=0)
        self.assertIn('cannot decode PNG', self.cli('asset', 'sprite', path).stderr)
        path = self.asset_png('oversized.png', 4096, 21, [b'\0' + b'\0' * 4096] * 21, color=0)
        self.assertIn('dimensions limited', self.cli('asset', 'sprite', path).stderr)

    def test_asset_project_directory_source_mode_pruning_and_clean(self):
        folder = self.dir / 'media'
        folder.mkdir()
        (folder / 'payload.bin').write_bytes(b'\x00\xff\x2a')
        source = 'from pyc64.assets import load\nprint(load("payload.bin"))\n'
        config = self.project(extra='[assets]\ndirectory="media"\n', entry='src/main.py', text=source)
        result = self.cli('build', '--project', config, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stderr)
        p = Program((self.dir / 'build/demo.PRG').read_bytes(), {})
        p.run()
        self.assertEqual(p.output, b"B'\\X00\\XFF*'\r")
        self.assertIn('Assets: 3 payload bytes', result.stdout)
        result = self.cli('clean', '--project', config, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((folder / 'payload.bin').read_bytes(), b'\x00\xff\x2a')
        result = self.cli('check', self.dir / 'src/main.py')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('cannot open asset file', result.stderr)
        self.module('unused.py', 'from c64.assets import sprite\ndef unseen() -> bytes:\n    return sprite("missing.png")\n')
        result, output, _ = self.invoke('import unused\nfrom c64 import assets\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        binary = output.read_bytes()
        result, output, _ = self.invoke('print("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), binary)

    def test_asset_diagnostics_limits_and_output_source_protection(self):
        for name, data in [('bad.spr', b'x'), ('empty.spr', b''), ('big.bin', b'x' * 32765),
                           ('bad.chr', b'x'), ('big.chr', b'\0' * 2056)]:
            (self.dir / name).write_bytes(data)
        for source, message in [
                ('load()', 'expects a path'), ('load("missing")', 'cannot open asset file'),
                ('load("big.bin")', '32764-byte'), ('load("big.bin", "extra")', 'expects a path'),
                ('sprite("bad.spr")', 'complete 64-byte'), ('sprite("empty.spr")', 'complete 64-byte'),
                ('charset("bad.chr")', 'eight-byte'), ('charset("big.chr")', '1..256'),
                ('load(42)', 'compile-time string'), ('load("")', 'nonempty'),
                ('sprite_multicolor("bad.spr", "bad")', 'palette')]:
            with self.subTest(source=source):
                self.reject('from pyc64.assets import load\nfrom c64.assets import sprite, charset, sprite_multicolor\nvalue = ' + source + '\n', message)
        self.reject('from pyc64.assets import load\npath = "big.bin"\nvalue = load(path)\n', 'compile-time string')
        self.reject('from c64.assets import address\naddress(1)\n', 'bytes')
        self.reject('from c64.assets import missing\n', 'unknown asset export')
        self.reject('from pyc64.assets import sprite\n', 'unknown asset export')
        (self.dir / 'protected.bin').write_bytes(b'original')
        source = self.module('main.py', 'from pyc64.assets import load\nvalue = load("protected.bin")\n')
        for option in ('-o', '--map'):
            result = self.cli('build', source, option, self.dir / 'protected.bin')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('overwrite an asset', result.stderr)
            self.assertEqual((self.dir / 'protected.bin').read_bytes(), b'original')
        (self.dir / 'fill.bin').write_bytes(b'x' * 6000)
        (self.dir / 'sprite.spr').write_bytes(b'\xff' * 64)
        result, output, _ = self.invoke('from pyc64.assets import load\nfrom c64.assets import sprite\n'
                                        'buffer = array(u8, 10000)\nfill = load("fill.bin")\nimage = sprite("sprite.spr")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes()[14], 0x4C)  # launcher jumps over VIC data


    def test_asset_cli_png_damage_options_and_no_partial_outputs(self):
        path = self.asset_png('valid.png', 24, 21, [b'\0' + b'\xff' * 24] * 21, color=0)
        output = self.dir / 'keep.spr'
        output.write_bytes(b'keep')
        original = path.read_bytes()
        for data in (original[:-1], original + b'x', original[:45] + bytes([original[45] ^ 1]) + original[46:], b'bad'):
            bad = self.dir / 'damaged.png'
            bad.write_bytes(data)
            result = self.cli('asset', 'sprite', bad, '-o', output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('invalid PNG', result.stderr)
            self.assertEqual(output.read_bytes(), b'keep')
        wrong = self.asset_png('wrong.png', 8, 8, [b'\0' + b'\0' * 8] * 8, color=0)
        result = self.cli('asset', 'sprite', wrong, '-o', output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('multiples of 24x21', result.stderr)
        for args in [(), ('unknown',), ('sprite',), ('sprite', path, '-o'),
                     ('sprite', path, '--palette', '000000,555555,aaaaaa,ffffff'),
                     ('sprite', path, '--project', 'file'), ('sprite', path, '-o', path),
                     ('sprite-multicolor', path, '--palette', 'gggggg,555555,aaaaaa,ffffff')]:
            with self.subTest(args=args):
                self.assertNotEqual(self.cli('asset', *args).returncode, 0)
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(self.cli('asset', '--help').returncode, 0)

    def test_bitmap_conversion_cell_order_palette_alpha_and_prepacked(self):
        # Alternating checkered and solid cells exercise both axes and the
        # majority/tie rule; transparent white belongs to palette index zero.
        rows = []
        expected = bytearray()
        screen = bytearray()
        for cell in range(1000):
            solid = (cell % 40 + cell // 40) % 2
            expected.extend(b'\0' * 8 if solid else b'\xaa' * 8)
            screen.append(1 if solid else 16)
        for y in range(200):
            row = bytearray([0])
            for x in range(320):
                solid = (x // 8 + y // 8) % 2
                white = solid or x % 2 == 0
                row.extend((255, 255, 255, 255) if white else (255, 255, 255, 127))
            rows.append(bytes(row))
        path = self.asset_png('picture.png', 320, 200, rows, color=6)
        for mode, extension, wanted in [('bitmap', '.hbm', expected), ('bitmap-screen', '.scr', screen)]:
            result = self.cli('asset', mode, path)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(path.with_suffix(extension).read_bytes(), wanted)
        p = self.compile('from c64.assets import bitmap, bitmap_screen, address\n'
                         'pixels = bitmap("picture.png")\nattributes = bitmap_screen("picture.png")\n'
                         'assert pixels == bitmap("picture.hbm")\nassert attributes == bitmap_screen("picture.scr")\n'
                         'location = address(pixels)\n')
        p.run(limit=3_000_000)
        self.assertEqual(p.value('location'), 0x2000)
        self.assertEqual(bytes(p.cpu.memory[0x2000:0x3F40]), expected)
        # Exact gray lands at index 2 in a custom palette, rather than Pepto's
        # nearest gray; ties for the unused foreground choose index zero.
        gray = self.asset_png('gray.png', 320, 200, [b'\0' + b'\x80' * 320] * 200, color=0)
        palette = ','.join(['000000', 'ffffff', '808080'] + ['ffffff'] * 13)
        result = self.cli('asset', 'bitmap-screen', gray, '--palette', palette)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(gray.with_suffix('.scr').read_bytes(), b'\x02' * 1000)

    def bitmap_files(self):
        (self.dir / 'picture.hbm').write_bytes(bytes(index % 256 for index in range(8000)))
        (self.dir / 'picture.scr').write_bytes(bytes(index % 256 for index in range(1000)))
        return ('from c64.assets import bitmap, bitmap_screen\nfrom c64 import bitmap as display\n'
                'pixels = bitmap("picture.hbm")\nattributes = bitmap_screen("picture.scr")\n')

    def test_bitmap_display_text_mode_restarts_and_saved_irq_flag(self):
        prefix = self.bitmap_files()
        for masked in (False, True):
            p = self.compile(prefix + 'shown = display.show(pixels, attributes)\n'
                             'bitmap_mode = peek(0xD011)\nbitmap_memory = peek(0xD018)\n'
                             'back = display.text()\ntext_mode = peek(0xD011)\ntext_memory = peek(0xD018)\n')
            for _ in range(2):
                p.cpu.p = (p.cpu.p | p.cpu.INTERRUPT) if masked else (p.cpu.p & ~p.cpu.INTERRUPT)
                p.cpu.memory[0xD011] = 0xC3
                p.cpu.memory[0xD016] = 0xD7
                p.cpu.memory[0xD018] = 0xFF
                p.cpu.memory[0xD800:0xDBE8] = [9] * 1000
                p.run(limit=2_000_000)
                self.assertEqual([p.value(name) for name in ('shown', 'bitmap_mode', 'bitmap_memory', 'back', 'text_mode', 'text_memory')],
                                 [1, 0xBB, 0x19, 1, 0x9B, 0x15])
                self.assertEqual(bytes(p.cpu.memory[0x0400:0x07E8]), (self.dir / 'picture.scr').read_bytes())
                self.assertEqual(p.cpu.memory[0xD800:0xDBE8], [9] * 1000)
                self.assertEqual(p.cpu.memory[0xD016], 0xCF)
                self.assertEqual(bool(p.cpu.p & p.cpu.INTERRUPT), masked)
                self.assertEqual(p.cpu.sp, 0xF0)
                self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
                p.restart()

    def test_bitmap_preflight_external_irq_and_output_preservation(self):
        prefix = self.bitmap_files()
        for source in ('shown = display.show(b"", attributes)\n',
                       'shown = display.show(pixels, b"bad")\n',
                       'shown = display.show(pixels, attributes)\nback = display.text()\n'):
            p = self.compile(prefix + source)
            p.cpu.memory[0xD01A] = 1
            p.cpu.memory[0xD011:0xD019] = list(range(8))
            p.run()
            self.assertEqual(p.value('shown'), 0)
            self.assertEqual(p.cpu.memory[0x0400:0x07E8], [255] * 1000)
            self.assertEqual(p.cpu.memory[0xD011:0xD019], list(range(8)))
        p = self.compile('from pyc64.assets import load\nfrom c64 import bitmap\n'
                         'pixels = load("picture.hbm")\nattributes = load("picture.scr")\n'
                         'shown = bitmap.show(pixels, attributes)\n')
        p.run()
        self.assertEqual(p.value('shown'), 0)  # generic binary has no VIC placement
        wrong = self.asset_png('wrong.png', 320, 192, [b'\0' + b'\0' * 320] * 192, color=0)
        output = self.dir / 'keep.hbm'
        output.write_bytes(b'keep')
        for mode in ('bitmap', 'bitmap-screen'):
            result = self.cli('asset', mode, wrong, '-o', output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('320x200', result.stderr)
            self.assertEqual(output.read_bytes(), b'keep')
        (self.dir / 'bad.hbm').write_bytes(b'x' * 7999)
        self.reject('from c64.assets import bitmap\nx = bitmap("bad.hbm")\n', '8000 bytes')
        self.reject('from c64.assets import bitmap_screen\nx = bitmap_screen("picture.scr", "bad")\n', 'sixteen')
        self.reject('from c64._video import mode\nmode(2)\n', '0..1')
        p = self.compile('from c64._video import mode\nvalue: u8 = 2\nmode(value)\n')
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()

    def test_bitmap_owned_raster_compare_latch_preserved(self):
        prefix = self.bitmap_files()
        for line, beam in ((300, 17), (100, 280)):
            p = self.compile(prefix + 'from c64 import irq\ndef tick():\n    pass\n'
                             f'installed = irq.on_raster({line}, tick)\n'
                             'shown = display.show(pixels, attributes)\nback = display.text()\n'
                             'while irq.active():\n    pass\n')
            device = IRQMemory(p, period=0)
            device.beam = beam
            p.run(limit=2_000_000, stop=lambda p: p.value('back') == 1)
            self.assertEqual(device.line, line)
            self.assertEqual(p.value('shown'), 1)
            self.assertTrue(all(masked for address, value, masked in device.writes if address == 0xD011))

    def test_bitmap_prefix_layout_large_storage_pruning_and_conflicts(self):
        prefix = self.bitmap_files()
        (self.dir / 'small.spr').write_bytes(b'\xff' * 64)
        p = self.compile(prefix + 'from c64.assets import sprite, address\n'
                         'small = sprite("small.spr")\nlarge = array(u8, 16000)\n'
                         'large[15999] = pixels[7999]\nlast = large[15999]\n'
                         'shown = display.show(pixels, attributes)\nsprite_location = address(small)\n')
        p.run(limit=2_000_000)
        self.assertEqual((p.value('last'), p.value('shown')), (63, 1))
        self.assertLess(p.value('sprite_location') + 64, 0x1000)
        # Separately headed sprites fill low RAM; the last one fits after the
        # bitmap instead of displacing it to the next, invisible 8 KB boundary.
        sprites = ''
        for index in range(17):
            (self.dir / f'{index}.spr').write_bytes(bytes([index]) * 64)
            sprites += f's{index} = sprite("{index}.spr")\na{index} = address(s{index})\n'
        p = self.compile(prefix + 'from c64.assets import sprite, address\n' + sprites +
                         'shown = display.show(pixels, attributes)\n')
        p.run(limit=2_000_000)
        self.assertEqual(p.value('shown'), 1)
        self.assertEqual(p.value('a16'), 0x3F80)
        self.assertEqual(p.cpu.memory[0x3F80:0x3FC0], [16] * 64)
        (self.dir / '17.spr').write_bytes(bytes([17]) * 64)
        self.reject(prefix + 'from c64.assets import sprite, address\n' + sprites +
                    'extra = sprite("17.spr")\n', 'VIC assets exceed')
        (self.dir / 'different.hbm').write_bytes(b'\xff' * 8000)
        self.reject(prefix + 'other = bitmap("different.hbm")\n', 'VIC assets exceed')
        self.module('unused.py', 'from c64.assets import bitmap\ndef unseen():\n    return bitmap("missing.png")\n')
        result, output, mapping = self.invoke('import unused\nfrom c64 import bitmap\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        binary = output.read_bytes()
        self.assertNotIn('asset ', mapping.read_text())
        result, output, _ = self.invoke('print("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), binary)

    def test_bitmap_source_library_matches_cpython_hardware_adapter(self):
        prefix = self.bitmap_files()
        p = self.compile(prefix + 'shown = display.show(pixels, attributes)\nback = display.text()\n')
        p.cpu.memory[0xD011] = 0x83
        p.cpu.memory[0xD016] = 0xD7
        p.run(limit=2_000_000)
        script = f'''import sys, types, json
sys.path.insert(0, {str(ROOT / 'stdlib')!r})
memory = bytearray(65536)
memory[0x0400:0x07e8] = bytes([255]) * 1000
memory[0xd011] = 0x83
memory[0xd016] = 0xd7
adapter = types.ModuleType('c64.memory')
adapter.peek = lambda position: memory[position]
def poke(position, value):
    memory[position] = value
adapter.poke = poke
sys.modules['c64.memory'] = adapter
irq = types.ModuleType('c64.irq')
irq.masked = False
def disable():
    old = irq.masked
    irq.masked = True
    return old
irq.disable = disable
irq.active = lambda: False
irq.restore = lambda value: setattr(irq, 'masked', bool(value))
sys.modules['c64.irq'] = irq
video = types.ModuleType('c64._video')
def mode(value):
    assert irq.masked
    memory[0xd011] = (memory[0xd011] & 0x87) | 0x18 | (0x20 if value else 0)
    memory[0xd016] = (memory[0xd016] & 0xef) | 8
    memory[0xd018] = 0x19 if value else 0x15
video.mode = mode
sys.modules['c64._video'] = video
import c64.assets as assets
assets.address = lambda data: 0x2000
from c64 import bitmap
pixels = assets.bitmap('picture.hbm')
attributes = assets.bitmap_screen('picture.scr')
assert bitmap.show(pixels, attributes) and not irq.masked
assert bitmap.text() and not irq.masked
before = bytes(memory)
assert not bitmap.show(pixels, b'bad')
assert bytes(memory) == before
print(json.dumps([list(memory[0x0400:0x07e8]), list(memory[0xd011:0xd019])]))
'''
        result = subprocess.run([sys.executable, '-c', script], cwd=self.dir, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        import json
        screen, registers = json.loads(result.stdout)
        self.assertEqual(screen, p.cpu.memory[0x0400:0x07E8])
        self.assertEqual(registers, p.cpu.memory[0xD011:0xD019])

    def test_bitmap_example_payloads_screen_and_restart(self):
        source = ROOT / 'examples/bitmap'
        shutil.copyfile(source / 'assets/landscape.png', self.dir / 'landscape.png')
        p = self.compile((source / 'main.py').read_text())
        for _ in range(2):
            p.run(limit=2_000_000)
            self.assertEqual([p.value(name) for name in ('shown', 'location', 'mode', 'memory')], [1, 8192, 32, 25])
            self.assertEqual(p.value('pixel_sum'), sum(p.cpu.memory[0x2000:0x3F40]) & 65535)
            self.assertEqual(p.value('attribute_sum'), sum(p.cpu.memory[0x0400:0x07E8]) & 65535)
            attributes = p.value('attributes')
            self.assertEqual(p.cpu.memory[0x0400:0x07E8], p.cpu.memory[attributes:attributes + 1000])
            p.restart()

    def music_file(self, name='tune.sid', version=2, location=0x1000,
                   songs=2, default=2, embedded=True, speed=0, timer=None):
        header = bytearray(118 if version == 1 else 124)
        header[:4] = b'PSID'
        struct.pack_into('>7H', header, 4, version, len(header),
                         0 if embedded else location, location, location + 48, songs, default)
        struct.pack_into('>I', header, 18, speed)
        payload = bytearray(128)
        state = location + 100
        # Save init's song argument and X/Y; reset a self-modifying RAM counter.
        init = bytes([0x8D, state & 255, state >> 8, 0x8E, (state + 2) & 255, state >> 8,
                      0x8C, (state + 3) & 255, state >> 8,
                      0xA9, 0, 0x8D, (state + 1) & 255, state >> 8,
                      0xA9, 15, 0x8D, 0x18, 0xD4, 0xA9, 0x11, 0x8D, 4, 0xD4,
                      0xA9, 0x12, 0x85, 0xFB, 0x85, 0xFC, 0x85, 0xFD, 0x85, 0xFE, 0x60])
        if timer is not None:
            init = init[:-1] + bytes([0xA9, timer & 255, 0x8D, 4, 0xDC,
                                     0xA9, timer >> 8, 0x8D, 5, 0xDC, 0x60])
        payload[:len(init)] = init
        # Update real SID registers, overwrite private zero page and bank BASIC
        # out, exercising the wrapper's restoration of foreground page/port.
        play = bytes([0xEE, (state + 1) & 255, state >> 8,
                      0xAD, (state + 1) & 255, state >> 8, 0x8D, 0, 0xD4,
                      0xA9, 0x42, 0x8D, 1, 0xD4,
                      0xA2, 0, 0xA9, 0xA7, 0x95, 2, 0xE8, 0xE0, 254, 0xD0, 0xF9,
                      0xA9, 0x35, 0x85, 1, 0x60])
        payload[48:48 + len(play)] = play
        data = bytes(header) + (struct.pack('<H', location) if embedded else b'') + bytes(payload)
        (self.dir / name).write_bytes(data)
        return data

    def test_music_psid_versions_subsongs_ports_zero_page_and_restart(self):
        for version, location, songs, default in [(1, 0x1000, 2, 2), (2, 0x1000, 2, 1),
                                                  (3, 0xC000, 256, 256), (4, 0xC000, 255, 255)]:
            with self.subTest(version=version):
                data = self.music_file(version=version, location=location, songs=songs,
                                       default=default, embedded=version % 2 == 1)
                p = self.compile('from c64 import music\n'
                                 'tune = music.load("tune.sid")\nsongs = music.songs(tune)\n'
                                 'default = music.default_song(tune)\nstarted = music.play(tune)\n'
                                 'while music.frames() < 8:\n    pass\n'
                                 f'counter = peek({location + 101})\nselected = peek({location + 100})\n'
                                 'frames = music.frames()\nlive = music.playing()\nstopped = music.stop()\n'
                                 'after = music.playing()\nagain = music.stop()\n')
                device = IRQMemory(p, period=10000)
                before = [(index * 17) & 255 for index in range(251)]
                p.cpu.memory[:251] = before
                p.cpu.memory[0], p.cpu.memory[1] = 0x2F, 0x37
                for _ in range(2):
                    p.run(limit=2_000_000)
                    self.assertEqual([p.value(n) for n in ('songs','default','started','selected','live','stopped','after','again')],
                                     [songs,default,1,default-1,1,1,0,0])
                    self.assertEqual(p.value('counter'), p.value('frames'))
                    self.assertGreaterEqual(p.value('frames'), 8)
                    self.assertEqual(p.cpu.memory[location + 102:location + 104], [0, 0])
                    self.assertEqual(p.cpu.memory[2:251], before[2:251])
                    self.assertEqual(p.cpu.memory[:2], [0x2F,0x37])
                    self.assertEqual(p.cpu.memory[0xFB:0xFF], [11,22,33,44])
                    self.assertEqual(p.cpu.sp, 0xF0)
                    self.assertEqual(p.cpu.memory[0xD400:0xD419], [0] * 25)
                    self.assertEqual(device.mask, 0)
                    self.assertEqual(bytes(p.cpu.memory[p.value('tune'):p.value('tune')+len(data)]), data)
                    p.restart()

    def test_music_user_raster_callback_coexistence_replacement_and_irq_off(self):
        self.music_file(default=1)
        for before in (True, False):
            source = '''from c64 import music, irq
from pyc64.types import u16, array
calls = array(u16, 1)
def callback():
    calls[0] = calls[0] + 1
tune = music.load("tune.sid")
'''
            if before:
                source += 'owned = irq.on_raster(100, callback)\n'
            source += 'started = music.play(tune)\n'
            if not before:
                source += 'owned = irq.on_raster(300, callback)\n'
            source += ('while music.frames() < 5:\n    pass\n'
                       'frames = music.frames()\ncount = calls[0]\nstopped = music.stop()\n'
                       'retained = irq.active()\nremoved = irq.off()\n')
            p = self.compile(source)
            device = IRQMemory(p, period=10000, cia_period=30001)
            p.run(limit=2_000_000)
            self.assertEqual([p.value(n) for n in ('owned','started','stopped','retained','removed')],[1]*5)
            self.assertGreaterEqual(p.value('count'), p.value('frames'))
            self.assertGreater(device.chained, 0)
        p = self.compile('from c64 import music, irq\ntune = music.load("tune.sid")\n'
                         'music.play(tune)\nremoved = irq.off()\nlive = music.playing()\n')
        IRQMemory(p, period=10000)
        p.run()
        self.assertEqual((p.value('removed'),p.value('live')),(1,0))
        self.assertEqual(p.cpu.memory[0xD400:0xD419], [0]*25)
        p = self.compile('from c64 import music, irq\n'
                         'def callback():\n    if music.frames() >= 5:\n        music.stop()\n'
                         'tune = music.load("tune.sid")\nirq.on_raster(100, callback)\n'
                         'music.play(tune)\nwhile music.playing():\n    pass\n'
                         'retained = irq.active()\ncount = music.frames()\nirq.off()\n')
        IRQMemory(p,period=10000)
        p.run()
        self.assertEqual(p.value('retained'),1)
        self.assertGreaterEqual(p.value('count'),5)
        self.assertEqual(p.cpu.memory[0xD400:0xD419],[0]*25)

    def test_music_failure_preserves_active_tune_and_external_irq_owner(self):
        self.music_file()
        p = self.compile('from c64 import music\ntune = music.load("tune.sid")\n'
                         'started = music.play_song(tune, 1)\n'
                         'wrong_song = music.play_song(tune, 3)\nwrong_data = music.play(b"bad")\n'
                         'while music.frames() < 4:\n    pass\nselected = peek(4196)\n'
                         'count = peek(4197)\nlive = music.playing()\n')
        IRQMemory(p, period=10000)
        p.run()
        self.assertEqual([p.value(n) for n in ('started','wrong_song','wrong_data','selected','live')],[1,0,0,0,1])
        self.assertGreaterEqual(p.value('count'),4)
        self.assertEqual(p.cpu.memory[0xD400:0xD419], [0]*25)  # return cleanup
        p = self.compile('from c64 import music\ntune = music.load("tune.sid")\nstarted = music.play(tune)\n')
        device = IRQMemory(p,period=0)
        device[0xD01A] = 1
        device[0xD400:0xD419] = [9]*25
        p.run()
        self.assertEqual(p.value('started'),0)
        self.assertEqual(device[0xD400:0xD419],[9]*25)
        self.assertEqual(device.mask,1)
        p = self.compile('from c64 import music, irq\ndef callback():\n    pass\n'
                         'unused = music.playing()\nirq.on_raster(100, callback)\n'
                         'poke(0xD404, 17)\nirq.off()\ncontrol = peek(0xD404)\n')
        IRQMemory(p,period=0)
        p.run()
        self.assertEqual(p.value('control'),17)  # IRQ teardown leaves unrelated notes alone

    def test_music_cia_default_and_custom_rates_preserve_kernal_service(self):
        for pal, timer in [(True, None), (False, None), (True, 0x6000)]:
            with self.subTest(pal=pal, timer=timer):
                self.music_file(default=1, speed=1, timer=timer)
                p = self.compile('''from c64 import music, irq
from pyc64.types import u16, array
calls = array(u16, 1)
def callback():
    calls[0] = calls[0] + 1
tune = music.load("tune.sid")
owned = irq.on_raster(100, callback)
started = music.play(tune)
while music.frames() < 48:
    pass
played = music.frames()
rasters = calls[0]
counter = peek(4197)
stopped = music.stop()
retained = irq.active()
irq.off()
''')
                device = CIAMemory(p, pal=pal, raster_cycles=19656 if pal else 17095)
                baseline = device.latches.copy()
                p.run(limit=2_000_000)
                self.assertEqual([p.value(n) for n in ('owned','started','stopped','retained')], [1]*4)
                self.assertEqual(p.value('played'), 48)
                self.assertEqual(p.value('counter'), 48)
                # Custom A is slower, while B keeps the original KERNAL rate.
                if timer:
                    self.assertGreater(device.kernal_calls, 60)
                    self.assertGreater(p.value('rasters'), 50)
                elif pal:
                    self.assertLess(p.value('rasters'), 45)
                    self.assertGreater(p.value('rasters'), 35)
                self.assertLessEqual(abs(device.kernal_calls - device.underflows[1]), 2)
                self.assertEqual(device.latches, baseline)
                self.assertEqual(device.controls, [1,8])
                self.assertEqual(device.cia_mask, 1)
                self.assertTrue(all(masked for _,_,masked in device.timer_writes))
                self.assertEqual(device[0x0314:0x0316], [0x31,0xEA])
                self.assertEqual(device[0xD400:0xD419], [0]*25)
                programmed = [(address,value) for address,value,_ in device.timer_writes]
                period = timer or (0x4025 if pal else 0x4295)
                self.assertIn((0xDC04, period & 255), programmed)
                self.assertIn((0xDC05, period >> 8), programmed)

    def test_music_cia_subsong_speed_bits_and_replacement(self):
        for version, speed, song, cia in [(1,1,33,True), (2,1,33,False),
                                          (2,0x80000000,33,True), (4,0x80000000,256,True),
                                          (3,2,1,False), (3,2,2,True)]:
            with self.subTest(version=version,song=song):
                self.music_file(version=version,songs=256,default=song,speed=speed)
                p = self.compile('from c64 import music\ntune = music.load("tune.sid")\n'
                                 'started = music.play(tune)\nwhile music.frames() < 4:\n    pass\n'
                                 'stopped = music.stop()\n')
                device = CIAMemory(p)
                p.run()
                self.assertEqual((p.value('started'),p.value('stopped')),(1,1))
                self.assertEqual(any(a == 0xDC0D and v == 0x83 for a,v,_ in device.timer_writes),cia)
        self.music_file(default=1,speed=1)
        p = self.compile('from c64 import music\ntune = music.load("tune.sid")\n'
                         'first = music.play_song(tune, 1)\nwhile music.frames() < 3:\n    pass\n'
                         'second = music.play_song(tune, 2)\nwhile music.frames() < 4:\n    pass\n'
                         'third = music.play_song(tune, 1)\nwhile music.frames() < 5:\n    pass\n'
                         'count = music.frames()\n')
        device = CIAMemory(p)
        p.run()
        self.assertEqual([p.value(n) for n in ('first','second','third','count')],[1,1,1,5])
        self.assertEqual(device.latches,[0x4025,0xFFFF])
        self.assertEqual(device.controls,[1,8])
        self.assertEqual(device.mask,0)

    def test_music_cia_busy_hardware_rejection_preserves_existing_music(self):
        self.music_file(default=1,speed=1)
        for address, value in [(0xDC0E,0),(0xDC0E,0x21),
                               (0xDC0F,1),(0xDC0F,0x40),(0xD01A,1)]:
            with self.subTest(address=address,value=value):
                p = self.compile('from c64 import music\ntune = music.load("tune.sid")\n'
                                 'first = music.play_song(tune,2)\n'
                                 f'poke({address},{value})\nsecond = music.play_song(tune,1)\n'
                                 'live = music.playing()\nselected = peek(4196)\n')
                device = CIAMemory(p,raster_cycles=0)
                # A raster mask is already owned by the VBI player, so test its
                # external-owner path separately below.
                if address == 0xD01A:
                    p = self.compile('from c64 import music\ntune = music.load("tune.sid")\n'
                                     'poke(0xD01A,1)\nsecond = music.play(tune)\n')
                    device = CIAMemory(p,raster_cycles=0)
                    device[0xD404] = 17
                    p.run()
                    self.assertEqual(p.value('second'),0)
                    self.assertEqual(device[0xD404],17)
                else:
                    p.run()
                    self.assertEqual([p.value(n) for n in ('first','second','live','selected')],[1,0,1,1])
                self.assertFalse(device.timer_writes and any(a == 0xDC0D for a,_,_ in device.timer_writes))
        p = self.compile('from c64 import music\ntune = music.load("tune.sid")\nstarted = music.play(tune)\n')
        device = CIAMemory(p,raster_cycles=0)
        device[0x0314] = 0x32
        p.run()
        self.assertEqual(p.value('started'),0)
        self.assertEqual(device.timer_writes,[])
        self.assertEqual(device.icr_reads,0)

    def test_music_cia_simultaneous_and_separate_irq_sources(self):
        self.music_file(default=1,speed=1)
        p = self.compile('''from c64 import music, irq
from pyc64.types import u16, array
calls = array(u16, 1)
def callback():
    calls[0] = calls[0] + 1
tune = music.load("tune.sid")
irq.on_raster(100, callback)
started = music.play(tune)
while music.frames() < 2:
    pass
played = music.frames()
rasters = calls[0]
music.stop()
irq.off()
''')
        device = CIAMemory(p,raster_cycles=0)
        p.run(stop=lambda p: p.value('started') == 1)
        # Inject exact simultaneous events, then each source alone, with the
        # counters paused so no incidental underflow changes the result.
        device.controls = [0,0]
        device.cia_flags, device.pending = 3,1
        ticks = next(int(line.split()[2][1:],16)+12 for line in (self.dir/'main.map').read_text().splitlines()
                     if line.startswith('runtime music '))
        def ready(tick, raster, kernal):
            return lambda p: not p.cpu.p & p.cpu.INTERRUPT and device[ticks] == tick and device[p.symbols['calls'][0]] == raster and device.kernal_calls == kernal
        p.run(stop=ready(1,1,1))
        device.cia_flags = 2
        p.run(stop=ready(1,1,2))
        device.pending = 1
        p.run(stop=ready(1,2,2))
        device.cia_flags = 1
        p.run()
        self.assertEqual((p.value('played'),p.value('rasters')),(2,2))

    def test_music_cia_callback_stop_irq_off_and_return_cleanup(self):
        self.music_file(default=1,speed=1)
        for ending in ('music.stop()', 'irq.off()', ''):
            p = self.compile('from c64 import music, irq\ntune = music.load("tune.sid")\n'
                             'started = music.play(tune)\nwhile music.frames() < 6:\n    pass\n'
                             + ending + '\n')
            device = CIAMemory(p)
            p.run()
            self.assertEqual(p.value('started'),1)
            self.assertEqual((device.controls,device.latches,device.cia_mask),([1,8],[0x4025,0xFFFF],1))
            self.assertEqual(device[0xD400:0xD419],[0]*25)
        p = self.compile('''from c64 import music, irq
def callback():
    if music.frames() >= 6:
        music.stop()
tune = music.load("tune.sid")
music.play(tune)
irq.on_raster(100, callback)
while music.playing():
    pass
retained = irq.active()
irq.off()
''')
        device = CIAMemory(p)
        p.run()
        self.assertEqual(p.value('retained'),1)
        self.assertEqual((device.controls,device.latches,device.cia_mask),([1,8],[0x4025,0xFFFF],1))
        self.assertEqual(device[0x0314:0x0316],[0x31,0xEA])
        p = self.compile('from c64 import music\ntune = music.load("tune.sid")\n'
                         'started = music.play(tune)\nassert not started\n')
        device = CIAMemory(p)
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        self.assertEqual((device.controls,device.latches,device.cia_mask),([1,8],[0x4025,0xFFFF],1))
        self.assertEqual(device[0xD400:0xD419],[0]*25)

    def test_music_cia_example_default_and_custom_periods(self):
        root = ROOT/'examples/music_cia'
        for name in ('theme.sid','slow.sid'):
            shutil.copyfile(root/'assets'/name,self.dir/name)
        for pal in (True,False):
            with self.subTest(pal=pal):
                p = self.compile((root/'main.py').read_text())
                device = CIAMemory(p,pal=pal,raster_cycles=19656 if pal else 17095)
                for _ in range(2):
                    p.run(limit=2_000_000)
                    self.assertEqual([p.value(n) for n in ('started','played','slow_started','slow_played',
                                                         'stopped','slow_stopped','removed','after_stop')],
                                     [1,48,1,48,1,1,1,0])
                    self.assertEqual(p.output,b'CIA CALLS: 48 48\r')
                    p.restart()
                    p.output.clear()

    def test_music_asset_validation_host_parity_and_output_protection(self):
        original = self.music_file()
        cases = [(0,b'RSID','RSID'),(4,b'\0\5','version'),(6,b'\0\0','data offset'),
                 (12,b'\0\0','init/play'),(14,b'\0\0','songs/start'),(16,b'\0\3','songs/start'),
                 (118,b'\0\1','MUS'),(122,b'\x42','one SID'),
                 (124,b'\0\xD0','payload must fit')]
        for offset, value, message in cases:
            with self.subTest(message=message):
                damaged = bytearray(original)
                damaged[offset:offset+len(value)] = value
                (self.dir/'bad.sid').write_bytes(damaged)
                self.reject('from c64 import music\ntune = music.load("bad.sid")\n',message)
        script = f'''import sys, types
sys.path.insert(0,{str(ROOT/'stdlib')!r})
adapter = types.ModuleType('c64.memory')
adapter.peek = lambda address: 0
adapter.poke = lambda address, value: None
sys.modules['c64.memory'] = adapter
from c64.music import load, songs, default_song
assert songs(load('tune.sid')) == 2 and default_song(load('tune.sid')) == 2
try:
    load('bad.sid')
except ValueError:
    pass
else:
    raise AssertionError('invalid PSID accepted')
'''
        result = subprocess.run([sys.executable,'-c',script],cwd=self.dir,text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        source = self.module('main.py','from c64 import music\ntune = music.load("tune.sid")\n')
        result = self.cli('build',source,'-o',self.dir/'tune.sid')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('overwrite an asset',result.stderr)
        self.assertEqual((self.dir/'tune.sid').read_bytes(),original)

    def rsid_file(self, name='real.sid', version=2, location=0x1000,
                  source='a', cli=False, chain=False, vector=None, port=0x37, nmi=None):
        header = bytearray(124)
        header[:4] = b'RSID'
        struct.pack_into('>7H',header,4,version,124,0,location,0,2,2)
        payload = bytearray(192)
        state, handler = location + 176, location + 80
        init = bytearray([0x8D,state & 255,state >> 8,0xA9,0,0x85,2,
                          0x8D,(state+1)&255,state>>8,
                          0xA9,15,0x8D,0x18,0xD4,0xA9,17,0x8D,4,0xD4,
                          0xA9,(vector if vector is not None else handler)&255,0x8D,0x14,3,
                          0xA9,(vector if vector is not None else handler)>>8,0x8D,0x15,3])
        if source == 'raster':
            init.extend(bytes.fromhex('a9 7f 8d 0d dc a9 00 8d 0e dc a9 64 8d 12 d0 a9 1b 8d 11 d0 a9 01 8d 1a d0'))
        else:
            index = 4 if source == 'a' else 6
            init.extend([0xA9,0x7F,0x8D,0x0D,0xDC,0xA9,0,0x8D,index,0xDC,
                         0xA9,0x60,0x8D,index+1,0xDC,0xA9,0x11,0x8D,0x0E+(source=='b'),0xDC,
                         0xA9,0x81 if source=='a' else 0x82,0x8D,0x0D,0xDC])
        if nmi is not None: init.extend([0xA9,nmi,0x8D,0x18,3])
        init.extend([0xA9,port,0x85,1])
        if cli: init.append(0x58)
        init.append(0x60)
        self.assertLessEqual(len(init),80)
        payload[:len(init)] = init
        irq = bytearray([0xAD,0x0D,0xDC,0xA9,1,0x8D,0x19,0xD0,
                         0xEE,(state+1)&255,state>>8,0xE6,2,0xA5,2,0x8D,0,0xD4,
                         0xA9,0xA7,0x85,0xFB,0x85,0xFC,0x85,0xFD,0x85,0xFE])
        irq.extend(bytes.fromhex('4c 31 ea') if chain else bytes.fromhex('68 a8 68 aa 68 40'))
        payload[80:80+len(irq)] = irq
        data = bytes(header) + struct.pack('<H',location) + payload
        (self.dir/name).write_bytes(data)
        return data

    def test_music_rsid_native_irq_versions_sources_subsongs_and_cleanup(self):
        for version,location,source,cli,chain in [(2,0x1000,'a',False,False),
                                                (3,0xC000,'b',True,False),
                                                (4,0x1000,'raster',True,True)]:
            with self.subTest(version=version,source=source):
                data = self.rsid_file(version=version,location=location,source=source,cli=cli,chain=chain)
                p = self.compile('from c64 import music\ntune = music.load("real.sid")\n'
                                 'started = music.play(tune)\nassert started\n'
                                 'while music.frames() < 8:\n    pass\n'
                                 f'count = peek({location+177})\nselected = peek({location+176})\n'
                                 'played = music.frames()\nlive = music.playing()\nstopped = music.stop()\n')
                device = CIAMemory(p)
                p.cpu.memory[:2] = [0x2F,0x37]
                p.cpu.memory[0xFA] = 53
                for _ in range(2):
                    p.run(limit=2_000_000)
                    self.assertEqual([p.value(n) for n in ('started','selected','live','stopped')],[1,1,1,1])
                    self.assertGreaterEqual(p.value('count'),p.value('played'))
                    self.assertGreaterEqual(p.value('played'),8)
                    self.assertEqual((device.controls,device.latches,device.cia_mask),([1,8],[0x4025,0xFFFF],1))
                    self.assertEqual(device[0xFB:0xFF],[11,22,33,44])
                    self.assertEqual(device[0xFA],53)
                    self.assertEqual(device[:2],[0x2F,0x37])
                    self.assertEqual(p.cpu.sp,0xF0)
                    self.assertEqual(device[0x0314:0x0316],[0x31,0xEA])
                    self.assertEqual(device[0xD400:0xD419],[0]*25)
                    self.assertEqual(bytes(device[p.value('tune'):p.value('tune')+len(data)]),data)
                    p.restart()

    def test_music_rsid_exclusive_irq_ownership_and_psid_replacement(self):
        self.rsid_file()
        self.music_file(default=1,speed=1)
        p = self.compile('''from c64 import music, irq
def callback():
    pass
real = music.load("real.sid")
tune = music.load("tune.sid")
irq.on_raster(100,callback)
blocked = music.play(real)
retained = irq.active()
irq.off()
masked = irq.disable()
blocked_critical = music.play(real)
irq.restore(masked)
first = music.play(tune)
while music.frames() < 3:
    pass
second = music.play(real)
while music.frames() < 4:
    pass
blocked_callback = irq.on_raster(100,callback)
blocked_line = irq.set_line(200)
bad_song = music.play_song(real,3)
live = music.playing()
third = music.play_song(real,1)
while music.frames() < 5:
    pass
selected = peek(4272)
fourth = music.play_song(tune,2)
while music.frames() < 6:
    pass
music.stop()
after = irq.on_raster(100,callback)
irq.off()
''')
        device = CIAMemory(p)
        p.run(limit=2_000_000)
        self.assertEqual([p.value(n) for n in ('blocked','retained','blocked_critical','first','second','blocked_callback',
                                             'blocked_line','bad_song','live','third','selected','fourth','after')],
                         [0,1,0,1,1,0,0,0,1,1,0,1,1])
        self.assertEqual((device.controls,device.latches,device.cia_mask),([1,8],[0x4025,0xFFFF],1))

    def test_music_rsid_invalid_init_rolls_back_vectors_timers_and_sid(self):
        for vector,port,nmi in [(0xEA31,0x37,None),(0x0FFF,0x37,None),(0x10C0,0x37,None),
                                (None,0x35,None),(None,0x37,0x48)]:
            with self.subTest(vector=vector,port=port,nmi=nmi):
                self.rsid_file(vector=vector,port=port,nmi=nmi)
                p = self.compile('from c64 import music, irq\ntune = music.load("real.sid")\n'
                                 'started = music.play(tune)\nlive = music.playing()\nowned = irq.active()\n')
                device = CIAMemory(p)
                p.run()
                self.assertEqual([p.value(n) for n in ('started','live','owned')],[0,0,0])
                self.assertEqual((device.controls,device.latches,device.cia_mask),([1,8],[0x4025,0xFFFF],1))
                self.assertEqual(device[0x0314:0x0316],[0x31,0xEA])
                self.assertEqual(device[0x0318:0x031A],[0,0])
                self.assertEqual(device[0xD400:0xD419],[0]*25)

    def test_music_rsid_headers_host_parity_and_unsupported_profiles(self):
        original = self.rsid_file()
        cases = [(4,b'\0\1','version'),(8,b'\x10\0','reserved'),(12,b'\x10\0','reserved'),
                 (18,b'\0\0\0\1','reserved'),(118,b'\0\2','BASIC'),(120,b'\xff','relocation'),(122,b'\x42','one SID'),
                 (124,b'\0\xd0','payload must fit')]
        script = f'''import sys
sys.path.insert(0,{str(ROOT/'stdlib')!r})
from c64.assets import sid
assert sid('real.sid')[:4] == b'RSID'
try:
    sid('bad.sid')
except ValueError:
    pass
else:
    raise AssertionError('unsupported RSID accepted')
'''
        for offset,value,message in cases:
            with self.subTest(message=message):
                damaged = bytearray(original)
                damaged[offset:offset+len(value)] = value
                (self.dir/'bad.sid').write_bytes(damaged)
                self.reject('from c64 import music\ntune = music.load("bad.sid")\n',message)
                result = subprocess.run([sys.executable,'-c',script],cwd=self.dir,text=True,capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr)

    def test_music_rsid_example_native_interrupts_and_restart(self):
        root = ROOT/'examples/music_rsid'
        shutil.copyfile(root/'assets/theme.sid',self.dir/'theme.sid')
        for pal in (True,False):
            p = self.compile((root/'main.py').read_text())
            device = CIAMemory(p,pal=pal,raster_cycles=19656 if pal else 17095)
            for _ in range(2):
                p.run(limit=2_000_000)
                self.assertEqual([p.value(n) for n in ('song_count','default','started','played',
                                                      'was_playing','stopped','after_stop','owned_after')],
                                 [2,1,1,48,1,1,0,0])
                self.assertEqual(p.output,b'RSID INTERRUPTS: 48\r')
                self.assertEqual(device[0xD400:0xD419],[0]*25)
                p.restart(); p.output.clear()

    def test_standalone_rsid_examples_profile_selection_and_pages(self):
        for mode in ('basic','busy','nmi','banked'):
            with self.subTest(mode=mode):
                source=ROOT/'examples/music_standalone/assets'/f'{mode}.sid'
                result=self.cli('sid',source,'--song','2','-o',self.dir/'full.PRG','--map',self.dir/'full.map')
                self.assertEqual(result.returncode,0,result.stderr)
                fields={line.split()[0]:line.split()[1:] for line in (self.dir/'full.map').read_text().splitlines()}
                self.assertEqual(fields['rsid_song'],['2'])
                self.assertEqual(fields['rsid_basic'],['1' if mode=='basic' else '0'])
                self.assertEqual(fields['rsid_driver'][0], '$C400' if mode in ('basic','banked') else '$0400')
                self.assertLessEqual(int(fields['rsid_driver'][1]),256)
                self.assertEqual((self.dir/'full.PRG').read_bytes()[:2],b'\x01\x08')
        shutil.copyfile(ROOT/'examples/music_standalone/assets/busy.sid',self.dir/'default.sid')
        result=self.cli('sid',self.dir/'default.sid')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue((self.dir/'default.PRG').exists())

    def test_standalone_rsid_resident_copy_aliases_and_ram_under_io(self):
        for load,length,page,pal in [(0x07E8,2000,4,True),(0x1000,40000,4,False),
                                     (0x1000,1024,0x0B,True),(0x1000,1024,8,True),
                                     (0xC000,4096,4,True),(0x1000,49000,4,True),
                                     (0x9000,20000,4,False),(0xC000,16384,4,True)]:
            with self.subTest(load=load,length=length,page=page):
                header=bytearray(self.rsid_file()[:124]);struct.pack_into('>H',header,10,load)
                header[120:122]=bytes([page,1])
                payload=bytes((i*71+13)&255 for i in range(length))
                source=self.dir/'copy.sid';source.write_bytes(header+struct.pack('<H',load)+payload)
                result=self.cli('sid',source,'-o',self.dir/'copy.PRG','--map',self.dir/'copy.map')
                self.assertEqual(result.returncode,0,result.stderr)
                fields={line.split()[0]:line.split()[1:] for line in (self.dir/'copy.map').read_text().splitlines()}
                memory=SidLaunchMemory([0]*65536)
                image=(self.dir/'copy.PRG').read_bytes()[2:];memory[0x0801:0x0801+len(image)]=image
                memory[0:2]=[0x2f,0x37];memory[0x02A6]=int(pal)
                resident=int(fields['rsid_driver'][0][1:],16)
                memory[resident:resident+256]=memory[0x0900:0x0A00]
                cpu=MPU(memory=memory,pc=resident);cpu.p|=cpu.INTERRUPT
                for _ in range(3_000_000):
                    if cpu.pc==load: break
                    cpu.step()
                else: self.fail('standalone resident copy did not reach native init')
                self.assertEqual(bytes(list.__getitem__(memory,slice(load,load+length))),payload)
                self.assertEqual(memory[1],0x37)
                self.assertEqual(memory[0xF9:0xFF],[0]*6)
                self.assertEqual((memory.io[0xDC04],memory.io[0xDC05]),(0x25,0x40) if pal else (0x95,0x42))
                self.assertFalse(cpu.p & cpu.INTERRUPT)
                self.assertEqual(cpu.a,1)  # file's default subsong 2, zero-based native argument

    def test_standalone_rsid_validation_and_output_protection(self):
        original=bytearray(self.rsid_file())
        cases=[(8,b'\x10\0','reserved'),(120,b'\xa0\1','relocation'),(120,b'\x10\1','overlaps'),
               (120,b'\xff\0','free RAM'),(120,b'\0\1','page count'),(120,b'\xc0\0','empty'),
               (120,b'\xcf\2','ROM'),(118,b'\0\2','BASIC init')]
        out=self.dir/'full.PRG';out.write_bytes(b'keep existing output')
        for offset,value,message in cases:
            with self.subTest(message=message):
                data=bytearray(original);data[offset:offset+len(value)]=value
                src=self.dir/'bad-full.sid';src.write_bytes(data)
                result=self.cli('sid',src,'-o',out)
                self.assertNotEqual(result.returncode,0)
                self.assertIn(message,result.stderr)
                self.assertEqual(out.read_bytes(),b'keep existing output')
        source=self.dir/'real.sid'
        for options in [('--song','3'),('--song','-1'),('--song','257'),('--song','2x'),('--vice','unknown')]:
            result=self.cli('sid',source,*options,'-o',out)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(out.read_bytes(),b'keep existing output')
        for options in [('-o',source),('--map',source),('-o',out,'--map',out)]:
            result=self.cli('sid',source,*options)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(source.read_bytes(),original)
        result=self.cli('sid',ROOT/'examples/music/assets/theme.sid','-o',out)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('RSID',result.stderr)

    def test_standalone_rsid_basic_bounds_and_single_prg_capacity(self):
        data=bytearray((ROOT/'examples/music_standalone/assets/basic.sid').read_bytes())
        data[126:128]=b'\x01\x08'  # cyclic first BASIC line link
        source=self.dir/'bad-basic.sid';source.write_bytes(data)
        result=self.cli('sid',source,'-o',self.dir/'bad.PRG')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('BASIC line',result.stderr)
        data=bytearray(self.rsid_file()[:126]);data.extend(b'\xea'*51000)
        source.write_bytes(data)
        result=self.cli('sid',source,'-o',self.dir/'bad.PRG')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('staged below I/O',result.stderr)
        self.assertFalse((self.dir/'bad.PRG').exists())

    def test_music_rsid_irq_off_return_trap_and_counter_wrap(self):
        self.rsid_file(source='raster')
        for ending in ('irq.off()', '', 'assert not started'):
            p = self.compile('from c64 import music, irq\ntune = music.load("real.sid")\n'
                             'started = music.play(tune)\nwhile music.frames() < 5:\n    pass\n' + ending + '\n')
            device = CIAMemory(p)
            if ending.startswith('assert'):
                with self.assertRaisesRegex(RuntimeError,'BRK guard'):
                    p.run()
            else:
                p.run()
            self.assertEqual((device.controls,device.latches,device.cia_mask),([1,8],[0x4025,0xFFFF],1))
            self.assertEqual(device[0x0314:0x0316],[0x31,0xEA])
            self.assertEqual(device.line,0x137)
            self.assertEqual(device[0xD400:0xD419],[0]*25)
        p = self.compile('from c64 import music\ntune = music.load("real.sid")\nstarted = music.play(tune)\n'
                         'while music.frames() != 0:\n    pass\nwrapped = music.frames()\nmusic.stop()\n')
        device = CIAMemory(p)
        p.run(stop=lambda p: p.value('started') == 1)
        ticks = next(int(line.split()[2][1:],16)+12 for line in (self.dir/'main.map').read_text().splitlines()
                     if line.startswith('runtime music '))
        device[ticks:ticks+2] = [0xFE,0xFF]
        p.run()
        self.assertEqual(p.value('wrapped'),0)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'C64 BASIC ROM required')
    def test_music_rsid_foreground_float_workspace_isolation(self):
        self.rsid_file(cli=True,chain=True)
        p = self.float_compile('from c64 import music\ntune = music.load("real.sid")\n'
                               'started = music.play(tune)\nvalue = 1.0\n'
                               'for i in range(40):\n    value = (value + 0.25) * 2.0 / 2.0\n'
                               'while music.frames() < 8:\n    pass\n'
                               'stopped = music.stop()\nprint(value)\n')
        device = CIAMemory(p)
        p.run(limit=2_000_000)
        self.assertEqual((p.value('started'),p.value('stopped')),(1,1))
        self.assertEqual(p.value('value'),11.0)
        self.assertEqual(p.output,b'11.0\r')
        self.assertEqual(device[0xFB:0xFF],[11,22,33,44])

    def test_music_linker_fixed_ram_graphics_and_unused_asset_pruning(self):
        self.music_file(location=0x2000)
        (self.dir/'font.chr').write_bytes(b'\xff'*8)
        p = self.compile('from c64 import music\nfrom c64.assets import charset, address\n'
                         'tune = music.load("tune.sid")\nfont = charset("font.chr")\n'
                         'position = address(font)\nstarted = music.play(tune)\n')
        p.run()
        self.assertEqual((p.value('position'),p.value('started')),(0x2800,1))
        self.assertEqual(p.cpu.memory[0x2800:0x2808],[255]*8)
        (self.dir/'pixels.hbm').write_bytes(b'\xff'*8000)
        self.reject('from c64 import music\nfrom c64.assets import bitmap\n'
                    'tune = music.load("tune.sid")\npixels = bitmap("pixels.hbm")\n','VIC assets exceed')
        self.module('unused.py','from c64.assets import sid\ndef unused():\n    return sid("missing.sid")\n')
        result, output, mapping = self.invoke('import unused\nprint("ready")\n')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotIn('runtime music',mapping.read_text())
        binary=output.read_bytes()
        result,output,_ = self.invoke('print("ready")\n')
        self.assertEqual(output.read_bytes(),binary)

    def test_music_example_native_updates_and_cleanup(self):
        root = ROOT/'examples/music'
        shutil.copyfile(root/'assets/theme.sid',self.dir/'theme.sid')
        p = self.compile((root/'main.py').read_text().replace('from c64 import music', 'from c64.audio import music'))
        IRQMemory(p,period=10000)
        p.run(limit=2_000_000)
        self.assertEqual([p.value(n) for n in ('song_count','default','started','played','was_playing','stopped','after_stop')],
                         [2,1,1,48,1,1,0])
        self.assertEqual(p.output,b'MUSIC FRAMES: 48\r')
        self.assertEqual(p.cpu.memory[0xD400:0xD419],[0]*25)
        p.restart();p.output.clear();p.run(limit=2_000_000)
        self.assertEqual(p.output,b'MUSIC FRAMES: 48\r')

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'C64 BASIC ROM required')
    def test_music_preserves_foreground_basic_float_workspace(self):
        self.music_file(default=1,speed=1)
        p = self.float_compile('from c64 import music\ntune = music.load("tune.sid")\n'
                               'started = music.play(tune)\nvalue = 1.0\n'
                               'for i in range(40):\n    value = (value + 0.25) * 2.0 / 2.0\n'
                               'while music.frames() < 8:\n    pass\ncount = music.frames()\n'
                               'stopped = music.stop()\nprint(value)\n')
        CIAMemory(p)
        p.run(limit=2_000_000)
        self.assertEqual(p.value('value'), 11.0)
        self.assertGreaterEqual(p.value('count'), 8)
        self.assertEqual(p.output, b'11.0\r')
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11,22,33,44])

    def tile_image(self, name, columns, rows, glyphs, indices):
        scan = [bytes([0]) + bytes(255 if glyphs[indices[y // 8 * columns + x // 8]][y % 8] & (1 << (7 - x % 8)) else 0
                                  for x in range(columns * 8)) for y in range(rows * 8)]
        return self.asset_png(name, columns * 8, rows * 8, scan, color=0)

    def test_tilemap_extraction_deduplication_row_order_and_prepacked(self):
        glyphs = [b'\0' * 8, b'\xff' * 8, b'\x81\x42\x24\x18\x18\x24\x42\x81']
        indices = [2, 1, 2, 0, 1, 2]
        rows = [bytes([0]) + bytes(255 if glyphs[indices[y // 8 * 3 + x // 8]][y % 8] & (1 << (7 - x % 8)) else 0
                                  for x in range(24)) for y in range(16)]
        path = self.asset_png('tiles.png', 24, 16, rows, color=0)
        expected_font = glyphs[2] + glyphs[1] + glyphs[0]
        expected_map = b'\x03\0\x02\0\x00\x01\x00\x02\x01\x00'
        for mode, extension, expected in [('tileset', '.chr', expected_font), ('tilemap', '.map', expected_map)]:
            result = self.cli('asset', mode, path)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(path.with_suffix(extension).read_bytes(), expected)
        p = self.compile('''from c64.assets import tileset, tilemap, address
from c64 import tiles
font = tileset("tiles.png")
data = tilemap("tiles.png")
assert font == tileset("tiles.chr") and data == tilemap("tiles.map")
font_location = address(font)
data_location = address(data)
w = tiles.width(data)
h = tiles.height(data)
ok = tiles.draw(data, font, 4, 7, 13)
''')
        for _ in range(2):
            p.cpu.memory[0xD018] = 0xA7
            p.cpu.memory[0xD011] = 0x9B
            p.cpu.memory[0xD016] = 0xC8
            p.run()
            self.assertEqual((p.value('w'), p.value('h'), p.value('ok')), (3, 2, 1))
            location = p.value('font_location')
            self.assertEqual(location % 2048, 0)
            self.assertEqual(bytes(p.cpu.memory[location:location + 24]), expected_font)
            start = p.value('data_location')
            self.assertEqual(bytes(p.cpu.memory[start:start + 10]), expected_map)
            for row in range(2):
                target = 7 * 40 + 4 + row * 40
                self.assertEqual(p.cpu.memory[0x0400 + target:0x0400 + target + 3], list(expected_map[4 + row * 3:7 + row * 3]))
                self.assertEqual(p.cpu.memory[0xD800 + target:0xD800 + target + 3], [13] * 3)
            self.assertEqual(p.cpu.memory[0xD018], 17 | location // 1024)
            self.assertEqual(p.cpu.memory[0xD011], 0x9B)
            self.assertEqual(p.cpu.memory[0xD016], 0xC8)
            p.restart()

    def test_tilemap_all_256_indices_wide_regions_and_257th_glyph(self):
        glyphs = [bytes([index]) + b'\0' * 7 for index in range(256)]
        path = self.tile_image('wide.png', 256, 1, glyphs, list(range(256)))
        for mode in ('tileset', 'tilemap'):
            result = self.cli('asset', mode, path)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.chr').read_bytes(), b''.join(glyphs))
        self.assertEqual(path.with_suffix('.map').read_bytes(), b'\0\x01\x01\0' + bytes(range(256)))
        p = self.compile('''from c64.assets import tileset, tilemap
from c64.tiles import width, height, draw, draw_region
font = tileset("wide.png")
data = tilemap("wide.png")
w = width(data)
h = height(data)
whole = draw(data, font, 0, 0, 1)
region = draw_region(data, font, 252, 0, 4, 1, 36, 24, 15)
''')
        p.run()
        self.assertEqual([p.value(name) for name in ('w', 'h', 'whole', 'region')], [256, 1, 0, 1])
        self.assertEqual(p.cpu.memory[0x07E4:0x07E8], [252, 253, 254, 255])
        self.assertEqual(p.cpu.memory[0xDBE4:0xDBE8], [15] * 4)
        glyphs.append(b'\x01\x01' + b'\0' * 6)
        path = self.tile_image('toomany.png', 16, 17, glyphs, list(range(257)) + [0] * 15)
        for mode in ('tileset', 'tilemap'):
            result = self.cli('asset', mode, path)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('256 distinct glyphs', result.stderr)
        self.reject('from c64.assets import tilemap\nvalue = tilemap("toomany.png")\n', '256 distinct glyphs')

    def test_tilemap_limits_and_map_header_diagnostics(self):
        glyphs = [b'\0' * 8]
        path = self.tile_image('large.png', 256, 31, glyphs, [0] * (256 * 31))
        result = self.cli('asset', 'tilemap', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.map').read_bytes(), b'\0\x01\x1f\0' + b'\0' * 7936)
        p = self.compile('from c64.assets import tileset, tilemap\nfrom c64.tiles import width, height\n'
                         'font = tileset("large.png")\ndata = tilemap("large.png")\nw = width(data)\nh = height(data)\n')
        p.run()
        self.assertEqual((p.value('w'), p.value('h')), (256, 31))
        path = self.tile_image('oversized.png', 256, 128, glyphs, [0] * 32768)
        result = self.cli('asset', 'tilemap', path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('32764-byte', result.stderr)
        result = self.cli('asset', 'tileset', path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(path.with_suffix('.chr').read_bytes(), b'\0' * 8)
        for data in (b'', b'\x01\0\x01\0', b'\0\0\x01\0\0', b'\x01\x01\x01\0\0',
                     b'\0\x01\0\x01', b'\xff\xff\xff\xff\0', b'\x01\0\x01\0\0\0'):
            with self.subTest(data=data):
                path = self.dir / 'bad.map'
                path.write_bytes(data)
                result = self.cli('asset', 'tilemap', path, '-o', self.dir / 'copy.map')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('exactly width * height', result.stderr)
                self.reject('from c64.assets import tilemap\nvalue = tilemap("bad.map")\n', 'exactly width * height')

    def test_tilemap_renderer_preflight_and_selected_index_validation(self):
        (self.dir / 'font.chr').write_bytes(b'\0' * 8 + b'\xff' * 8)
        prefix = '''from c64.assets import tileset
from c64.tiles import draw, draw_region, width, height, set_charset
font = tileset("font.chr")
'''
        cases = [
            ('data = b""\nok = draw(data, font, 0, 0, 1)\n', False),
            ('data = b"\\x00\\x01\\x00\\x01"\nok = draw(data, font, 0, 0, 1)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x02"\nok = draw(data, font, 0, 0, 1)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x01"\nok = draw(data, font, 39, 0, 1)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x01"\nok = draw(data, font, 0, 25, 1)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x01"\nok = draw(data, font, 0, 0, 16)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x01"\nok = draw_region(data, font, 2, 0, 1, 1, 0, 0, 1)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x01"\nok = draw_region(data, font, 1, 0, 2, 1, 0, 0, 1)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x01"\nok = draw_region(data, font, 0, 0, 0, 1, 0, 0, 1)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x01"\nok = draw_region(data, font, 0, 1, 1, 1, 0, 0, 1)\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\x01"\nok = draw_region(data, font, 65535, 0, 1, 1, 0, 0, 1)\n', False),
            ('ok = set_charset(b"bad")\n', False),
            ('data = b"\\x02\\0\\x01\\0\\0\\xff"\nok = draw_region(data, font, 0, 0, 1, 1, 0, 0, 2)\n', True)]
        for source, valid in cases:
            with self.subTest(source=source):
                p = self.compile(prefix + source)
                p.cpu.memory[0x0400:0x07E8] = [55] * 1000
                p.cpu.memory[0xD800:0xDBE8] = [11] * 1000
                p.cpu.memory[0xD011:0xD019] = [0xFF] * 8
                p.run()
                self.assertEqual(p.value('ok'), int(valid))
                if not valid:
                    self.assertEqual(p.cpu.memory[0x0400:0x07E8], [55] * 1000)
                    self.assertEqual(p.cpu.memory[0xD800:0xDBE8], [11] * 1000)
                    self.assertEqual(p.cpu.memory[0xD011:0xD019], [0xFF] * 8)
                else:
                    self.assertEqual(p.cpu.memory[0x0400:0x0402], [0, 55])
                    self.assertEqual(p.cpu.memory[0xD800:0xD802], [2, 11])
        (self.dir / 'raw.bin').write_bytes(b'\0' * 8)
        p = self.compile('from pyc64.assets import load\nfrom c64.tiles import set_charset\n'
                         'buffer = array(u8, 15000)\nfont = load("raw.bin")\nok = set_charset(font)\n')
        p.cpu.memory[0xD018] = 0xAB
        p.run()
        self.assertEqual(p.value('ok'), 0)
        self.assertEqual(p.cpu.memory[0xD018], 0xAB)

    def test_tilemap_renderer_matches_cpython_hardware_adapter(self):
        (self.dir / 'font.chr').write_bytes(bytes(range(32)))
        p = self.compile('''from c64.assets import tileset, address
from c64.tiles import draw_region
font = tileset("font.chr")
location = address(font)
data = b"\\x03\\0\\x02\\0\\x00\\x01\\x02\\x02\\x03\\x01"
ok = draw_region(data, font, 1, 0, 2, 2, 38, 23, 7)
''')
        p.cpu.memory[0x0400:0x07E8] = [55] * 1000
        p.cpu.memory[0xD800:0xDBE8] = [11] * 1000
        p.cpu.memory[0xD018] = 0xA1
        p.run()
        script = f'''import sys, types, json
sys.path.insert(0, {str(ROOT / 'stdlib')!r})
memory = bytearray(65536)
memory[0x0400:0x07e8] = bytes([55]) * 1000
memory[0xd800:0xdbe8] = bytes([11]) * 1000
memory[0xd018] = 0xa1
adapter = types.ModuleType('c64.memory')
adapter.peek = lambda position: memory[position]
def poke(position, value):
    memory[position] = value
adapter.poke = poke
sys.modules['c64.memory'] = adapter
import c64.assets as assets
assets.address = lambda data: {p.value('location')}
from c64 import tiles
data = bytes([3, 0, 2, 0, 0, 1, 2, 2, 3, 1])
assert tiles.draw_region(data, bytes(range(32)), 1, 0, 2, 2, 38, 23, 7)
print(json.dumps([list(memory[0x0400:0x07e8]), list(memory[0xd800:0xdbe8]), memory[0xd018]]))
for location in (0, 0x1000, 0x1800, 0x2001, 0x4000):
    tiles.address = lambda data, location=location: location
    before = bytes(memory)
    assert not tiles.set_charset(bytes(range(32)))
    assert bytes(memory) == before
'''
        result = subprocess.run([sys.executable, '-c', script], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        import json
        screen, colors, pointer = json.loads(result.stdout)
        self.assertEqual(screen, p.cpu.memory[0x0400:0x07E8])
        self.assertEqual(colors, p.cpu.memory[0xD800:0xDBE8])
        self.assertEqual(pointer, p.cpu.memory[0xD018])

    def test_tilemap_import_aliases_reexports_and_unused_pruning(self):
        self.module('exports.py', 'from c64.assets import tileset as glyphs, tilemap as layout\n'
                    'from c64.tiles import width as columns\n')
        (self.dir / 'font.chr').write_bytes(b'\0' * 8)
        (self.dir / 'data.map').write_bytes(b'\x01\0\x01\0\0')
        p = self.compile('from exports import glyphs, layout, columns\nfont = glyphs("font.chr")\n'
                         'data = layout("data.map")\nw = columns(data)\n')
        p.run()
        self.assertEqual(p.value('w'), 1)
        result, output, mapping = self.invoke('import c64.tiles\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        binary = output.read_bytes()
        self.assertNotIn('function c64.tiles.', mapping.read_text())
        result, output, _ = self.invoke('print("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), binary)

    def test_tilemap_example_viewport_and_restart(self):
        source = ROOT / 'examples/tilemap'
        shutil.copyfile(source / 'assets/level.png', self.dir / 'level.png')
        p = self.compile((source / 'main.py').read_text())
        for _ in range(2):
            p.run(limit=2_000_000)
            self.assertEqual([p.value(name) for name in ('columns', 'rows', 'drawn', 'font_selected', 'color_sum')],
                             [48, 30, 1, 1, 13000])
            start = p.value('level')
            expected = [p.cpu.memory[start + 4 + row * 48 + column] for row in range(3, 28) for column in range(4, 44)]
            self.assertEqual(p.cpu.memory[0x0400:0x07E8], expected)
            self.assertEqual(p.cpu.memory[0xD800:0xDBE8], [13] * 1000)
            self.assertEqual(p.value('screen_sum'), sum(expected))
            self.assertEqual(p.value('first_tile'), expected[0])
            self.assertEqual(p.value('last_tile'), expected[-1])
            self.assertEqual(p.cpu.memory[0xD018] & 14, p.value('font_location') // 1024)
            p.restart()

    def irq_program(self, source, period=500, cia_period=0):
        p = self.compile(source)
        device = IRQMemory(p, period=period, cia_period=cia_period)
        return p, device

    def test_irq_raster_example_shared_frames_and_restart(self):
        for period in (113, 337, 751):
            with self.subTest(period=period):
                p, device = self.irq_program((ROOT / 'examples/raster/main.py').read_text(), period, 997)
                for _ in range(2):
                    p.run()
                    self.assertEqual(p.output, b'RASTER: 8 49\r')
                    self.assertEqual(p.value('count'), 8)
                    self.assertEqual(p.value('callback_result'), 49)
                    self.assertEqual(device.mask & 1, 0)
                    self.assertEqual(device[0x0314:0x0316], [0x31, 0xEA])
                    self.assertTrue(all(item[2] for item in device.writes))
                    self.assertEqual(p.cpu.sp, 0xF0)
                    self.assertEqual(device[0xFB:0xFF], [11, 22, 33, 44])
                    p.restart()
                    p.output.clear()
                    device.writes.clear()

    def test_irq_preemption_at_every_shared_function_instruction(self):
        source = (ROOT / 'examples/raster/main.py').read_text()
        source = source.replace('if ticks[0] == 8:', 'if ticks[0] == 1:')
        source = source.replace('count == 8 and callback_result == 49', 'count == 1 and callback_result == 21')
        baseline, _ = self.irq_program(source, period=0)
        functions = {name: int(address[1:], 16) for line in (self.dir / 'main.map').read_text().splitlines()
                     if (parts := line.split()) and parts[0] == 'function'
                     for _, name, address in [parts]}
        start, end = functions['shared_work'], functions['on_frame']
        baseline.run(stop=lambda p: p.cpu.pc == start)
        trace = []
        while start <= baseline.cpu.pc < end:
            trace.append(baseline.cpu.pc)
            baseline.cpu.step()
        binary = (self.dir / 'main.PRG').read_bytes()
        self.assertGreater(len(trace), 100)
        for step, pc in enumerate(trace):
            with self.subTest(step=step, pc=hex(pc)):
                p = Program(binary, baseline.symbols)
                device = IRQMemory(p, period=0)
                p.run(stop=lambda program: program.cpu.pc == start)
                for _ in range(step):
                    p.cpu.step()
                device.pending |= 1
                p.run()
                self.assertEqual(p.output, b'RASTER: 1 21\r')
                self.assertEqual(device.delivered, 1)
                self.assertEqual(p.cpu.sp, 0xF0)
                self.assertEqual(device[0xFB:0xFF], [11, 22, 33, 44])

    def test_irq_line_replacement_external_ownership_and_cleanup(self):
        source = '''from c64 import irq
ticks = array(u8, 1)
def first():
    ticks[0] = 1
def second():
    ticks[0] = 2
    irq.off()
empty = irq.off()
inactive_line = irq.set_line(42)
installed = irq.on_raster(256, first)
changed = irq.on_raster(511, second)
while irq.active():
    pass
value = ticks[0]
'''
        p, device = self.irq_program(source, period=1500)
        device.beam = 17  # Read raster MSB differs from the programmed compare.
        device.mask = 6
        p.run()
        self.assertEqual([p.value(name) for name in ('empty', 'inactive_line', 'installed', 'changed', 'value')],
                         [0, 0, 1, 1, 2])
        self.assertEqual(device.line, 511)
        self.assertEqual(device.mask, 6)
        self.assertEqual(device[0x0314:0x0316], [0x31, 0xEA])
        p, device = self.irq_program('from c64 import irq\ndef callback():\n    pass\nresult = irq.on_raster(256, callback)\n', period=0)
        device.mask = 1
        before = device[0x0314:0x0316]
        p.run()
        self.assertEqual(p.value('result'), 0)
        self.assertEqual(device.mask, 1)
        self.assertEqual(device[0x0314:0x0316], before)
        self.assertEqual(device.writes, [])
        # Normal return and BRK cleanup also uninstall a still-active callback.
        for tail, guard in [('', False), ('assert False\n', True)]:
            p, device = self.irq_program('from c64 import irq\ndef callback():\n    pass\n'
                                         'result = irq.on_raster(100, callback)\n' + tail, period=0)
            if guard:
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
            else:
                p.run()
            self.assertEqual(device.mask & 1, 0)
            self.assertEqual(device[0x0314:0x0316], [0x31, 0xEA])

    def test_irq_critical_sections_cia_chaining_and_nested_enable_guard(self):
        source = '''from c64 import irq
ticks = array(u8, 1)
def callback():
    saved = irq.disable()
    ticks[0] = ticks[0] + 1
    irq.restore(saved)
    if ticks[0] == 3:
        irq.off()
saved = irq.disable()
nested = irq.disable()
installed = irq.on_raster(100, callback)
for index in range(200):
    pass
before = ticks[0]
irq.restore(saved)
while irq.active():
    pass
after = ticks[0]
'''
        p, device = self.irq_program(source, period=503, cia_period=257)
        p.cpu.p &= ~p.cpu.INTERRUPT
        p.run()
        self.assertEqual([p.value(name) for name in ('saved', 'nested', 'before', 'after')], [0, 1, 0, 3])
        self.assertGreater(device.chained, 0)
        self.assertFalse(p.cpu.p & p.cpu.INTERRUPT)
        p, device = self.irq_program('from c64 import irq\ndef callback():\n    irq.restore(False)\n'
                                     'irq.on_raster(100, callback)\nwhile irq.active():\n    pass\n', period=300)
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        self.assertEqual(device.mask & 1, 0)
        self.assertEqual(device[0x0314:0x0316], [0x31, 0xEA])

    def test_irq_large_frames_properties_bytes_and_hardware_helpers(self):
        source = '''from c64 import irq, sid
class Meter:
    _value: u16 = 0
    @property
    def value(self):
        return self._value
    @value.setter
    def value(self, word: u16):
        self._value = word
results = array(u16, 1)
payload = b"xABx"
def work(seed: u16) -> u16:
    values = array(u16, 300)
    small = [seed, seed, u16(17)]
    meter = Meter()
    meter.value = seed
    total: u16 = 0
    for index in range(300):
        values[index] = seed + u16(index)
    for item in values:
        total += item
    return total + small[2] + meter.value
def callback():
    assert b"AB" in payload
    results[0] = work(7)
    assert sid.frequency(0, 1234)
    irq.off()
assert irq.on_raster(100, callback)
value = work(1234)
while irq.active():
    pass
recorded = results[0]
print(value, recorded)
'''
        for period in (1301, 2903):
            p, device = self.irq_program(source, period=period)
            p.run(limit=3000000)
            self.assertEqual(p.output, b'23085 46974\r')
            self.assertEqual(device[0xD400:0xD402], [0xD2, 4])
            mapping = (self.dir / 'main.map').read_text()
            backup = re.search(r'^runtime irq_backup \$[0-9A-F]+ (\d+)$', mapping, re.M)
            self.assertGreater(int(backup[1]), 600)

    def test_irq_imported_callbacks_pruning_and_unsafe_diagnostics(self):
        self.module('handlers.py', 'from pyc64.types import u8, array\nfrom c64 import irq\n'
                    'ticks = array(u8, 1)\ndef frame():\n    ticks[0] = ticks[0] + 1\n    irq.off()\n    return None\n')
        self.module('exports.py', 'from handlers import frame as tick\n')
        p, device = self.irq_program('from c64 import irq\nfrom exports import tick\n'
                                    'assert irq.on_raster(100, tick)\nwhile irq.active():\n    pass\n', period=333)
        p.run()
        self.assertEqual(p.value('handlers.ticks'), 1)
        self.assertIn('function handlers.frame ', (self.dir / 'main.map').read_text())
        result, output, mapping = self.invoke('from c64 import irq\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime raster_irq ', mapping.read_text())
        baseline = output.read_bytes()
        result, output, mapping = self.invoke('print("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), baseline)
        for body, message in [
                ('print("unsafe")', 'ROM-dependent'),
                ('input()', 'ROM-dependent'),
                ('value = float(1)', 'floating-point'),
                ('value = 1.0 < 2.0', 'floating-point'),
                ('value = int("2")', 'ROM-dependent'),
                ('count: u8 = 3\n    value = bytes(count)', 'runtime bytes'),
                ('data = b"x"\n    value = data * 2', 'allocate immutable'),
                ('data = b"xyz"\n    value = data[1:]', 'allocate immutable'),
                ('assert False, "unsafe"', 'failure messages'),
                ('irq.on_raster(100, callback)', 'foreground code')]:
            with self.subTest(body=body):
                self.reject('from c64 import irq\ndef callback():\n    ' + body +
                            '\nirq.on_raster(100, callback)\n', message)
        self.module('unsafe.py', 'def helper():\n    print("unsafe")\ndef callback():\n    helper()\n')
        self.reject('from c64 import irq\nfrom unsafe import callback\nirq.on_raster(100, callback)\n',
                    'ROM-dependent', diagnostic='unsafe.py')
        self.reject('from c64 import irq\nclass P:\n    value: float = 1.0\n'
                    'def callback():\n    instance = P()\nirq.on_raster(100, callback)\n', 'float fields')

    def test_irq_callback_selectors_line_guards_and_stack_limits(self):
        for source, message in [
                ('def callback(value):\n    pass\nirq.on_raster(100, callback)\n', 'zero-argument'),
                ('def callback():\n    return 1\nirq.on_raster(100, callback)\n', 'return None'),
                ('irq.on_raster(100, 42)\n', 'static zero-argument'),
                ('irq.on_raster(100, missing)\n', 'source function'),
                ('def callback():\n    pass\nirq.on_raster(512, callback)\n', '0..511'),
                ('def callback():\n    pass\nirq.on_raster(100)\n', 'line and a callback')]:
            with self.subTest(source=source):
                self.reject('from c64 import irq\n' + source, message)
        self.reject('from c64.irq import missing\n', 'unknown c64.irq export')
        definitions = 'from c64 import irq\ndef f0() -> u8:\n    return 0\n'
        definitions += ''.join(f'def f{i}() -> u8:\n    return f{i - 1}()\n' for i in range(1, 41))
        self.reject(definitions + 'def callback():\n    f40()\nirq.on_raster(100, callback)\n', 'limit 80')
        for operation in ('on_raster(line, callback)', 'set_line(line)'):
            p, device = self.irq_program('from c64 import irq\ndef callback():\n    pass\n'
                                         'line: u16 = 512\nirq.' + operation + '\n', period=0)
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()
            self.assertEqual(device.writes, [])
        p = self.compile('from c64 import irq\nsaved = irq.disable()\nirq.restore(saved)\n')
        p.cpu.p &= ~p.cpu.INTERRUPT
        p.run()
        self.assertFalse(p.cpu.p & p.cpu.INTERRUPT)
        self.assertNotIn('runtime raster_irq ', (self.dir / 'main.map').read_text())

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure BASIC ROM for IRQ/foreground float checks')
    def test_irq_preserves_foreground_basic_rom_workspace_and_restart(self):
        p = self.float_compile((ROOT / 'examples/raster_floats/main.py').read_text())
        device = IRQMemory(p, period=487, cia_period=977)
        for _ in range(2):
            p.run(limit=3000000)
            self.assertEqual(p.value('total'), 162.5)
            self.assertEqual(p.output, b'IRQ FLOAT: 5 162.5\r')
            self.assertEqual(device[2:0x90], [index ^ 0x55 for index in range(142)])
            self.assertEqual(device[0x0300:0x0302], [0x3A, 0xA4])
            self.assertEqual(device[0x0314:0x0316], [0x31, 0xEA])
            p.restart()
            p.output.clear()

    def reu_program(self, source, attached=True, size=512 * 1024):
        p = self.compile(source, options=('--target', 'c64-reu'))
        device = REUMemory(p.cpu.memory, attached=attached, size=size)
        device.cpu = p.cpu
        p.cpu.memory = device
        return p, device

    def test_reu_example_bank_crossing_modes_and_restart(self):
        p, device = self.reu_program((ROOT / 'examples/reu/main.py').read_text())
        for _ in range(2):
            p.run()
            self.assertEqual(p.output, b'REU: TRUE TRUE TRUE FALSE\rSWAP: 680 21\rFILL: 1360 REPEAT: 6\r')
            self.assertEqual([p.value(name) for name in ('checksum', 'replacement', 'fill_sum', 'repeat_sum')],
                             [680, 21, 1360, 6])
            self.assertEqual(device.expansion[0x2FFFC:0x30002], bytes(range(1, 7)))
            self.assertEqual(device.expansion[0x40100:0x40110], b'\x55' * 16)
            self.assertTrue(all(masked for _, _, masked in device.writes))
            self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
            self.assertEqual(p.cpu.sp, 0xF0)
            p.restart()
            p.output.clear()
            device.writes.clear()

    def test_reu_present_absent_and_interrupt_state(self):
        source = (ROOT / 'examples/reu_extended/main.py').read_text()
        for attached in (True, False):
            p, device = self.reu_program(source, attached=attached, size=16 * 1024 * 1024)
            p.run(limit=3000000)
            self.assertEqual(p.value('checksum'), 32640 if attached else 0)
            self.assertEqual(p.value('filled'), int(attached))
            self.assertEqual(p.value('end_value'), 204 if attached else 0)
            self.assertEqual(p.value('last_value'), 90 if attached else 0)
            self.assertEqual(p.value('wrap_rejected'), int(attached))
            self.assertEqual(len(device.transfers), 516 if attached else 0)
        for attached in (True, False):
            for masked in (False, True):
                with self.subTest(attached=attached, masked=masked):
                    p, device = self.reu_program('from c64 import reu\nresult = reu.present()\n', attached=attached)
                    device.registers[0:4] = [0xF0, 0x10, 0x12, 0x34]
                    before = device.registers.copy()
                    if masked:
                        p.cpu.p |= p.cpu.INTERRUPT
                    else:
                        p.cpu.p &= ~p.cpu.INTERRUPT
                    p.run()
                    self.assertEqual(p.value('result'), int(attached))
                    self.assertEqual(bool(p.cpu.p & p.cpu.INTERRUPT), masked)
                    self.assertEqual(device.registers, before)
                    self.assertEqual(device.transfers, [])
                    self.assertNotIn(0xDF00, device.reads)
                    self.assertTrue(all(item[2] for item in device.writes))

    def test_reu_transfer_modes_fixed_addresses_and_python_oracle(self):
        initial_c64 = bytes(range(17))
        initial_reu = bytes(range(32, 49))
        for mode in range(4):
            reu_values = initial_c64 if mode == 3 else initial_reu
            for control in (0, 64, 128, 192):
                for count in (1, 2, 17):
                    with self.subTest(mode=mode, control=control, count=count):
                        p, device = self.reu_program(f'from c64 import reu\nresult = reu.transfer(0xC000, 0x100, 3, {count}, {mode}, {control})\n')
                        device[0xC000:0xC011] = initial_c64
                        device.expansion[0x30100:0x30111] = reu_values
                        expected_c64, expected_reu = bytearray(initial_c64), bytearray(reu_values)
                        good = True
                        for index in range(count):
                            cindex = 0 if control & 128 else index
                            eindex = 0 if control & 64 else index
                            a, b = expected_c64[cindex], expected_reu[eindex]
                            if mode == 0:
                                expected_reu[eindex] = a
                            elif mode == 1:
                                expected_c64[cindex] = b
                            elif mode == 2:
                                expected_c64[cindex], expected_reu[eindex] = b, a
                            elif a != b:
                                good = False
                                break
                        p.run()
                        self.assertEqual(p.value('result'), int(good))
                        self.assertEqual(bytes(device[0xC000:0xC011]), expected_c64)
                        self.assertEqual(device.expansion[0x30100:0x30111], expected_reu)
                        self.assertEqual(device.transfers, [(0xC000, 0x30100, count, mode, control)])

    def test_reu_preflight_zero_count_no_device_and_native_guards(self):
        source = '''from c64 import reu
zero = reu.store(65535, 65535, 255, 0)
bad_mode = reu.transfer(0xC000, 0, 0, 1, 4, 0)
bad_control = reu.transfer(0xC000, 0, 0, 1, 0, 1)
c64_wrap = reu.fetch(65535, 0, 0, 2)
reu_wrap = reu.store(0xC000, 65535, 255, 2)
'''
        p, device = self.reu_program(source)
        before = device.registers.copy()
        p.run()
        self.assertEqual([p.value(name) for name in ('zero', 'bad_mode', 'bad_control', 'c64_wrap', 'reu_wrap')],
                         [1, 0, 0, 0, 0])
        self.assertEqual(device.registers, before)
        self.assertEqual(device.reads, [])
        self.assertEqual(device.writes, [])
        p, device = self.reu_program('from c64 import reu\nresult = reu.store(0xC000, 0, 0, 1)\n', attached=False)
        p.run()
        self.assertEqual(p.value('result'), 0)
        self.assertEqual(device.transfers, [])
        self.assertNotIn(0xDF01, [item[0] for item in device.writes])
        for count, mode, control in [(0, 0, 0), (1, 4, 0), (1, 0, 1)]:
            p, device = self.reu_program(f'from c64._reu import transfer\nresult = transfer(0xC000, 0, 0, {count}, {mode}, {control})\n')
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()
            self.assertEqual(device.writes, [])

    def test_reu_native_argument_snapshots_and_status_clearing(self):
        p, device = self.reu_program('''from c64 import reu
from c64._reu import transfer
calls = array(u16, 1)
def value(digit: u8, result: u16) -> u16:
    calls[0] = u16(calls[0] * 10 + u16(digit))
    assert reu.fill(0xC020, 0, 0, 1)
    return result
result = transfer(value(1, 0xC000), value(2, 0x1234), u8(value(3, 5)), value(4, 4), u8(value(5, 0)), u8(value(6, 0)))
total = calls[0]
''')
        device[0xC000:0xC004] = [0, 255, 42, 128]
        device.registers[0] = 0xF0
        device.registers[9] = 0xE0
        p.cpu.p &= ~p.cpu.INTERRUPT
        p.run()
        self.assertEqual(p.value('total'), 123456 & 65535)
        self.assertEqual(p.value('result') & 96, 64)
        self.assertEqual(device.transfers[-1], (0xC000, 0x51234, 4, 0, 0))
        self.assertEqual(device.expansion[0x51234:0x51238], b'\x00\xff\x2a\x80')
        self.assertEqual(device.registers[0] & 0xE0, 0)
        self.assertEqual(device.registers[9], 0)
        self.assertFalse(p.cpu.p & p.cpu.INTERRUPT)
        self.assertTrue(all(item[2] for item in device.writes))

    def test_reu_all_byte_values_large_fill_and_last_address(self):
        p, device = self.reu_program('''from c64 import reu
all_bytes = reu.store(0xC000, 65500, 1, 256)
restored = reu.fetch(0xC200, 65500, 1, 256)
same = reu.verify(0xC200, 65500, 1, 256)
large = reu.fill(0xC400, 65500, 3, 65535)
last = reu.store(0xC400, 65535, 255, 1)
''', size=16 * 1024 * 1024)
        device[0xC000:0xC100] = range(256)
        device[0xC400] = 0x5A
        p.run()
        self.assertEqual([p.value(name) for name in ('all_bytes', 'restored', 'same', 'large', 'last')], [1] * 5)
        self.assertEqual(bytes(device[0xC200:0xC300]), bytes(range(256)))
        self.assertEqual(device.expansion[0x3FFDC:0x3FFDC + 65535], b'\x5A' * 65535)
        self.assertEqual(device.expansion[-1], 0x5A)

    def test_reu_target_gates_reexports_pruning_and_launch(self):
        options = ('--target', 'c64-reu')
        self.reject('from c64._reu import missing\n', 'unknown c64._reu export', options=options)
        self.reject('from c64._reu import transfer\nresult = transfer(0, 0, 0)\n', 'expects 6 arguments', options=options)
        definitions = 'from c64._reu import transfer\ndef f0() -> u8:\n    return 0\n'
        definitions += ''.join(f'def f{i}() -> u8:\n    return f{i - 1}()\n' for i in range(1, 64))
        self.reject(definitions + 'result = transfer(0xC000, 0, 0, 1, 0, f63())\n', 'stack', options=options)
        for statement in ['from c64 import reu', 'import c64.reu', 'from c64.reu import store',
                          'from c64._reu import present']:
            self.reject(statement + '\n', "requires target 'c64-reu'")
        self.module('bridge.py', 'from c64 import reu\n')
        self.reject('import bridge\n', "requires target 'c64-reu'", diagnostic='bridge.py')
        p = self.compile('from c64 import reu\nvalue = reu.present()\n', options=('--target', 'c64-ultimate'))
        device = REUMemory(p.cpu.memory)
        device.cpu = p.cpu
        p.cpu.memory = device
        p.run()
        self.assertEqual(p.value('value'), 1)
        result, output, _ = self.invoke('print("ready")\n', options=('--target', 'c64-reu'))
        self.assertEqual(result.returncode, 0, result.stderr)
        baseline = output.read_bytes()
        result, output, _ = self.invoke('from c64 import reu\nprint("ready")\n', options=('--target', 'c64-reu'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), baseline)
        executable = self.dir / ('vice with spaces.exe' if os.name == 'nt' else 'vice with spaces')
        shutil.copy2(COMPILER, executable)
        result, _, _ = self.invoke('print("ready")\n', command='run',
                                   options=('--target', 'c64-reu', '--vice', str(executable)))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown option '-reu'", result.stderr)

    def test_reu_host_python_library_hardware_adapter(self):
        script = '''import sys, types
from hardware import REUMemory
memory = types.ModuleType('c64.memory')
device = REUMemory([0] * 65536)
memory.peek = lambda address: device[address]
memory.poke = lambda address, value: device.__setitem__(address, int(value))
sys.modules['c64.memory'] = memory
from c64 import reu
assert reu.present()
device[0xC000:0xC006] = [0, 255, 42, 128, 254, 1]
assert reu.store(0xC000, 65532, 2, 6)
assert reu.fetch(0xC100, 65532, 2, 6)
assert reu.verify(0xC100, 65532, 2, 6)
device[0xC100] = 99
assert not reu.verify(0xC100, 65532, 2, 6)
device[0xC200:0xC206] = range(1, 7)
assert reu.swap(0xC200, 65532, 2, 6)
assert bytes(device[0xC200:0xC206]) == bytes([0, 255, 42, 128, 254, 1])
assert device.expansion[0x2fffc:0x30002] == bytes(range(1, 7))
device[0xC400] = 85
assert reu.fill(0xC400, 256, 4, 16)
assert device.expansion[0x40100:0x40110] == b'\\x55' * 16
before = len(device.writes)
for parameters in [(-1, 0, 0, 1, 0, 0), (0, -1, 0, 1, 0, 0), (0, 0, 256, 1, 0, 0),
                   (0, 0, 0, -1, 0, 0), (0, 0, 0, 65536, 0, 0), (0, 0, 0, 1, 4, 0),
                   (0, 0, 0, 1, 0, 1), (65535, 0, 0, 2, 1, 0), (0, 65535, 255, 2, 0, 0)]:
    assert reu.transfer(*parameters) is False
assert reu.store(65535, 65535, 255, 0)
assert len(device.writes) == before
device.attached = False
assert not reu.present() and not reu.store(0xC000, 0, 0, 1)
'''
        env = dict(os.environ, PYTHONPATH=str(ROOT / 'tests'))
        result = subprocess.run([sys.executable, '-c', script], cwd=ROOT / 'stdlib',
                                env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_bytes_membership_constants_and_python_precedence(self):
        self.iteration_oracle('''from typing import Final
PRESENT: Final[bool] = b"\\x00\\xff" in b"A\\x00\\xffB"
ABSENT: Final[bool] = 255 not in b"ABC"
class Packet:
    present: bool = b"" in b""
packet = Packet()
print(PRESENT, ABSENT, packet.present, b"long" not in b"A")
data = b"\\x00\\x01AB\\xff"
print(1 + 64 in data, 128 | 127 in data, not 65 in data, not 66 not in data)
print(65 in data and 255 in data, 99 not in data or 99 in data)
print((65 in data) == True, (65 not in data) == False)
from enum import IntEnum
class Byte(IntEnum):
    A = 65
print(Byte.A in data, True in data, False in data)
''')
        result, _, mapping = self.invoke('from typing import Final\nVALUE: Final = b"x" in b"xyz"\nprint(VALUE)\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())
        self.assertNotIn('runtime bytes_work ', mapping.read_text())

    def test_bytes_membership_all_values_and_subsequences_python_oracle(self):
        data = bytes(range(256))
        for batch in range(0, 256, 8):
            source = [f'data = {data!r}', 'needle: u16 = 0']
            for value in range(batch, batch + 8):
                source += [f'needle = {value}', 'print(needle in data, needle not in data)']
            with self.subTest(batch=batch):
                self.iteration_oracle('\n'.join(source) + '\n')
        for data in [b'', b'A', b'\x00A\xffA\x00', b'aaaaa', bytes(range(256))]:
            for needle in [b'', b'\x00', b'A', b'\xffA', b'\x00A', b'aa', b'aaa', b'ABC', bytes(range(256))]:
                with self.subTest(data=data, needle=needle):
                    self.iteration_oracle(f'data = {data!r}\nneedle = {needle!r}\nprint(needle in data, needle not in data)\n')

    def test_bytes_membership_operand_order_properties_and_inference(self):
        self.iteration_oracle('''class Packet:
    data: bytes = b"old"
    calls: u16 = 0
def needle(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 1)
    packet.data = b"old"
    return packet.data
def container(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 2)
    packet.data = b"new"
    assert b"new" in packet.data
    return b"old-new"
def contains(needle, data):
    return needle in data
def contains_byte(needle, data):
    return needle not in data
packet = Packet()
print(needle(packet) in container(packet), packet.calls, packet.data)
print(contains(b"old", b"old-new"), contains(b"\\xff", b"\\xff"))
print(contains(b"absent", b"old-new"))
print(contains_byte(255, b"\\xff"), contains_byte(254, b"\\xff"))
''')
        self.iteration_oracle('''class Packet:
    _data: bytes = b"saved"
    reads: u8 = 0
    @property
    def data(self):
        self.reads += 1
        return self._data
def mutate(packet: Packet):
    packet._data = b"different"
    return b"saved-different"
packet = Packet()
print(packet.data in mutate(packet), packet.reads, packet._data)
print((b"AB" + b"CD") in b"--ABCD--", b"BC" in b"ABCD"[1:3])
data = b"ABCAB"
needle = b"AB"
print(needle in bytes(data), b"AB" in data)
''')

    def test_bytes_membership_guards_short_circuit_and_diagnostics(self):
        for value in (-1, 256, 65535):
            for op in ('in', 'not in'):
                with self.subTest(value=value, op=op):
                    kind = 'i16' if value < 0 else 'u16'
                    p = self.compile(f'needle: {kind} = {value}\ndata = b""\nresult = needle {op} data\nprint("unreachable")\n')
                    with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                        p.run()
                    self.assertEqual(p.output, b'')
        self.iteration_oracle('needle: u16 = 256\ndata = b"ABC"\na = False\nb = True\nprint(a and needle in data, b or needle not in data)\n')
        for source, message in [
                ('result = -1 in b""\n', '0..255'),
                ('result = 256 not in b""\n', '0..255'),
                ('result = "A" in b"ABC"\n', 'bytes membership'),
                ('result = 1.0 in b"ABC"\n', 'bytes membership'),
                ('result = b"A" in "ABC"\n', 'bytes membership'),
                ('result = 1 in [1, 2]\n', 'bytes membership'),
                ('result = 65 in b"ABC" == True\n', 'chained comparisons'),
                ('result = 65 == 65 in b"ABC"\n', 'chained comparisons'),
                ('result = 65 not True\n', "expected 'in'")]:
            with self.subTest(source=source):
                self.reject(source, message)

    def test_bytes_membership_nested_search_restart_and_pruning(self):
        source = '''data = b"ABCAB\\x00\\xff"
present = b"AB" in data
absent = 254 not in data
for index in range(1100):
    present = (data.find(b"AB") + 65) in data
    absent = b"AB" not in b"DCBA"
print(present, absent)
'''
        p = self.compile(source)
        for _ in range(2):
            p.run(limit=3000000)
            self.assertEqual(p.output, b'TRUE TRUE\r')
            p.restart()
            p.output.clear()
        result, _, mapping = self.invoke(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())
        p = self.compile('result = ord(input("Byte? ")) in bytes(input("Data? "), "ascii")\nprint(result)\n')
        p.keyboard = list(b'A\rABC\r')
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'BYTE? A\rDATA? ABC\rTRUE\r')
        result, output, _ = self.invoke('print("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        baseline = output.read_bytes()
        result, output, _ = self.invoke('def unused(data: bytes) -> bool:\n    return b"x" in data\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), baseline)

    def test_bytes_search_literal_constants_and_fields(self):
        self.module('search_data.py', '''from typing import Final
FIRST: Final = b"A\\x00\\xffAB".find(b"AB")
LAST: Final[i16] = b"A\\x00\\xffAB".rfind(65)
COUNT: Final[u16] = b"aaaaa".count(b"aa")
'''.replace('from typing import Final', 'from typing import Final\nfrom pyc64.types import i16, u16'))
        p = self.compile('''from search_data import FIRST, LAST, COUNT
class Result:
    position: i16 = b"abc".index(b"b")
    total: u16 = b"abc".count(b"")
result = Result()
print(FIRST, LAST, COUNT, result.position, result.total)
print(b"abc".find(b""), b"abc".rfind(b""), b"abc".count(b"", 4))
print(b"abc".startswith(b"", 4), b"abc".endswith(b"", 3), b"abc".find(255))
''')
        p.run()
        self.assertEqual(p.output, b'3 3 2 1 4\r0 3 0\rFALSE TRUE -1\r')
        result, _, mapping = self.invoke('from typing import Final\nPOSITION: Final = b"abc".find(b"b")\nvalue = POSITION\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())

    def test_bytes_search_runtime_python_oracle_all_byte_values_and_overlap(self):
        self.iteration_oracle('''data = b"\\x00\\xffAb\\x00\\xffAb\\x00"
needle = b"\\x00\\xff"
print(data.find(needle), data.rfind(needle), data.index(needle), data.rindex(needle))
print(data.count(needle), data.count(0), data.count(b""), data.find(255), data.rfind(0))
print(data.startswith(needle), data.endswith(b"b\\x00"), data.find(b"ab"), data.find(b"Ab"))
repeat = b"aaaaa"
print(repeat.find(b"aa"), repeat.rfind(b"aa"), repeat.count(b"aa"))
''')
        data = bytes(range(256))
        for batch in range(0, 256, 8):
            source = [f'data = {data!r}', 'needle: u16 = 0']
            for value in range(batch, batch + 8):
                source += [f'needle = {value}', 'print(data.find(needle), data.rfind(needle), data.count(needle))']
            with self.subTest(batch=batch):
                self.iteration_oracle('\n'.join(source) + '\n')

    def test_bytes_search_runtime_bounds_empty_needles_and_python_oracle(self):
        rng = random.Random(1982)
        bounds = [None, -32768, -100, -5, -1, 0, 1, 5, 6, 99, 65535]
        cases = [(start, end) for start in bounds for end in bounds]
        rng.shuffle(cases)
        for data in [b'', b'A', b'\x00A\xffA\x00']:
            for needle in [b'', b'A', b'\x00', b'\xffA', b'long needle']:
                for batch in range(0, len(cases), 16):
                    source = [f'data = {data!r}', f'needle = {needle!r}',
                              'ss: i16 = 0', 'su: u16 = 0', 'es: i16 = 0', 'eu: u16 = 0']
                    for start, end in cases[batch:batch + 16]:
                        args = ['needle']
                        for prefix, value in zip(('s', 'e'), (start, end)):
                            if value is None:
                                args.append('None')
                            else:
                                name = prefix + ('u' if value > 32767 else 's')
                                source.append(f'{name} = {value}')
                                args.append(name)
                        tail = ', '.join(args)
                        source.append('print(' + ', '.join(f'data.{method}({tail})' for method in
                                                          ('find', 'rfind', 'count', 'startswith', 'endswith')) + ')')
                    with self.subTest(data=data, needle=needle, batch=batch):
                        self.iteration_oracle('\n'.join(source) + '\n')

    def test_bytes_search_receiver_argument_order_fields_and_nested_calls(self):
        self.iteration_oracle('''class Packet:
    data: bytes = b"AB\\x00AB\\xff"
    calls: u16 = 0
def receiver(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 1)
    return packet.data
def needle(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 2)
    packet.data = b"changed"
    return b"AB"
def start(packet: Packet) -> i16:
    packet.calls = u16(packet.calls * 10 + 3)
    return 1
def end(packet: Packet) -> u16:
    packet.calls = u16(packet.calls * 10 + 4)
    return 6
packet = Packet()
found = receiver(packet).find(needle(packet), start(packet), end(packet))
print(found, packet.calls, packet.data)
def search(data, needle):
    return data.rfind(needle)
print(search(b"ABCAB", b"AB"), search(b"ABCA", b"B"))
print((b"AB" + b"AB").count(b"AB"), b"ABC"[::-1].find(b"B"))
from enum import IntEnum
class Bound(IntEnum):
    START = 0
    END = 5
    BYTE = 65
data = b"ABCAB"
print(data[Bound.START:Bound.END].find(Bound.BYTE, Bound.START, Bound.END))
''')
        self.iteration_oracle('''class Packet:
    _data: bytes = b"ABAB"
    reads: u8 = 0
    @property
    def data(self):
        self.reads += 1
        return self._data
def change(packet: Packet):
    packet._data = b"changed"
    return b"AB"
packet = Packet()
print(packet.data.rfind(change(packet)), packet.reads, packet._data)
''')

    def test_bytes_search_input_bounds_recursion_pruning_and_stack_guards(self):
        p = self.compile('data = b"xAxA"\n'
                         'result = data.find(ord(input("Byte? ")), ord(input("Start? ")) - 48, ord(input("End? ")) - 48)\n'
                         'print(result)\n')
        p.keyboard = list(b'A\r2\r4\r')
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'BYTE? A\rSTART? 2\rEND? 4\r3\r')
        self.reject('def receiver() -> bytes:\n    value = receiver().find(b"x")\n    return b"x"\n'
                    'result = receiver().find(b"x")\n', 'recursion')
        definitions = 'def f0() -> i16:\n    return 1\n'
        definitions += ''.join(f'def f{i}() -> i16:\n    return f{i - 1}()\n' for i in range(1, 64))
        self.reject(definitions + 'data = b"abc"\nresult = data.find(b"a", 0, f63())\n', 'stack')
        result, baseline, _ = self.invoke('print("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = baseline.read_bytes()
        result, output, mapping = self.invoke('def unused(data: bytes) -> i16:\n    return data.find(b"x")\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())
        self.assertEqual(output.read_bytes(), expected)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_bytes_search_float_input_argument_snapshots_and_rom_state(self):
        p = self.float_compile('''data = b"ABCAB"
result = data.find(b"AB", int(float(input("Start? "))), int(float(input("End? "))))
present = int(float(input("Byte? "))) in data
print(result, 1.5, data.rfind(b"AB"), present)
''')
        for _ in range(2):
            p.keyboard = list(b'1\r5\r65\r')
            p.run()
            self.assertEqual(p.output, b'START? 1\rEND? 5\rBYTE? 65\r3 1.5 3 TRUE\r')
            self.assertEqual(p.cpu.memory[0x0002:0x0090], [index ^ 0x55 for index in range(142)])
            self.assertEqual(p.cpu.memory[0x0300:0x0302], [0x3A, 0xA4])
            p.restart()
            p.output.clear()

    def test_bytes_search_example_and_restart(self):
        p = self.compile((ROOT / 'examples/bytes_search/main.py').read_text())
        for _ in range(2):
            p.run()
            self.assertEqual([p.value(name) for name in ('first', 'last', 'indexed', 'reverse_indexed',
                                                        'occurrences', 'zeroes', 'empty_count')],
                             [0, 4, 2, 6, 2, 3, 10])
            self.assertIn(b'POSITIONS: 0 4 2 6\rCOUNTS: 2 3 10\rEDGES: TRUE TRUE\r', p.output)
            p.restart()
            p.output.clear()

    def test_bytes_search_index_and_integer_needle_runtime_guards(self):
        for source in ['data = b"abc"\nresult = data.index(b"x")\n',
                       'data = b"abc"\nresult = data.rindex(b"", 4)\n',
                       'data = b"abc"\nneedle: i8 = -1\nresult = data.find(needle)\n',
                       'data = b""\nneedle: u16 = 256\nresult = data.count(needle)\n']:
            with self.subTest(source=source):
                p = self.compile(source + 'print("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertEqual(p.output, b'')
        for source, message in [('result = b"abc".index(b"x")\n', 'subsequence was not found'),
                                ('result = b"abc".find(-1)\n', '0..255'),
                                ('result = b"abc".count(256)\n', '0..255'),
                                ('result = b"abc".find("a")\n', 'bytes search requires'),
                                ('result = b"abc".startswith(65)\n', 'require bytes'),
                                ('result = b"abc".find(b"a", 1.0)\n', 'bounds require integers'),
                                ('result = b"abc".find()\n', 'expect a needle'),
                                ('result = b"abc".count(b"a", 0, 3, 1)\n', 'expect a needle'),
                                ('result = b"abc".split(b"a")\n', 'unsupported bytes method')]:
            with self.subTest(source=source):
                self.reject(source, message)

    def test_bytes_search_no_arena_repeated_calls_long_values_and_restart(self):
        source = 'data = ' + repr(b'\xff' * 8188) + '\n'
        source += '''first = data.find(255)
last = data.rfind(255)
total = data.count(255)
empty_count = data.count(b"")
result: i16 = -1
for index in range(1100):
    result = data.find(255, 8000)
print(first, last, total, empty_count, result)
'''
        p = self.compile(source)
        for _ in range(2):
            p.run(limit=3000000)
            self.assertEqual(p.output, b'0 8187 8188 8189 8000\r')
            p.restart()
            p.output.clear()
        result, _, mapping = self.invoke(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())

    def test_bytes_slice_literals_defaults_none_and_constants(self):
        self.module('slice_data.py', '''from typing import Final
DATA: Final[bytes] = b"A\\x00\\xffBC"
REVERSE: Final = DATA[::-1]
MIDDLE: Final = DATA[1:-1]
''')
        p = self.compile('''from slice_data import DATA, REVERSE, MIDDLE
copy = DATA[None:None:None]
print(DATA[:], REVERSE, MIDDLE, copy)
print(DATA[-1::-2], DATA[:-1:-1], DATA[-1:None:-1], DATA[99:-99:-3])
print(DATA[-99:99], DATA[4:1], DATA[1:3:65535], DATA[::-32768])
''')
        p.run()
        self.assertEqual(p.output,
                         b"B'A\\X00\\XFFBC' B'CB\\XFF\\X00A' B'\\X00\\XFFB' B'A\\X00\\XFFBC'\r"
                         b"B'C\\XFFA' B'' B'CB\\XFF\\X00A' B'C\\X00'\r"
                         b"B'A\\X00\\XFFBC' B'' B'\\X00' B'C'\r")
        result, _, mapping = self.invoke('from typing import Final\nDATA: Final = b"abc"[::-1]\ncopy = DATA[:]\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('constant DATA bytes length=3', mapping.read_text())
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())

    def test_bytes_slice_runtime_python_oracle_boundaries_and_random_cases(self):
        rng = random.Random(6510)
        bounds = [None, -32768, -300, -10, -1, 0, 1, 8, 300, 32767, 65535]
        steps = [None, -32768, -300, -3, -1, 1, 2, 300, 32767, 65535]
        cases = [(None, None, step) for step in steps]
        cases += [(start, stop, step) for start, stop, step in
                  [(None, -1, -1), (-1, None, -1), (-1, -1, -1),
                   (-32768, 65535, 2), (65535, -32768, -2),
                   (0, -1, -1), (None, 0, -1), (None, 1, -32768)]]
        cases += [(rng.choice(bounds), rng.choice(bounds), rng.choice(steps)) for _ in range(102)]
        for data in [b'', b'\xff', b'A\x00\xffBC\x80\x00DE', bytes(range(256))]:
            for batch in range(0, len(cases), 30):
                source = [f'data = {data!r}', 'ss: i16 = 0', 'su: u16 = 0',
                          'es: i16 = 0', 'eu: u16 = 0', 'ks: i16 = 1', 'ku: u16 = 1']
                for start, stop, step in cases[batch:batch + 30]:
                    indices = []
                    for prefix, value in zip(('s', 'e', 'k'), (start, stop, step)):
                        if value is None:
                            indices.append('None')
                        else:
                            name = prefix + ('u' if value > 32767 else 's')
                            source.append(f'{name} = {value}')
                            indices.append(name)
                    source.append(f'print(data[{":".join(indices)}])')
                with self.subTest(length=len(data), batch=batch):
                    self.iteration_oracle('\n'.join(source) + '\n')

    def test_bytes_slice_functions_fields_nested_values_and_operand_order(self):
        self.iteration_oracle('''class Packet:
    _data: bytes = b"\\x00\\xffABC"
    calls: u16 = 0
    @property
    def data(self):
        self.calls = u16(self.calls * 10 + 1)
        return self._data
def bound(packet: Packet, value: i16, tag: u16) -> i16:
    packet.calls = u16(packet.calls * 10 + tag)
    packet._data = b"changed"
    return value
def cut(data, start: i16, stop: i16, step: i16):
    return data[start:stop:step]
packet = Packet()
saved = packet.data[bound(packet, -1, 2):bound(packet, -6, 3):bound(packet, -2, 4)]
print(saved, packet._data, packet.calls)
other = cut(saved, 0, 3, 2)
print(other, saved, cut(b"ABCDEFG", 1, 6, 2)[::-1])
items = [saved, other]
print(items[0][1:], (saved + other)[::2], (2 * saved)[::-2])
''')
        self.iteration_oracle('''from enum import IntEnum
class Bound(IntEnum):
    START = -1
    STOP = -7
    STEP = -2
data = b"AB\\x00\\xffCD"
start: i8 = -1
stop: i8 = -128
step: i8 = -2
print(data[Bound.START:Bound.STOP:Bound.STEP], data[start:stop:step])
print(data[False:True:True], data[(None):(None):(None)])
''')

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_bytes_slice_float_input_bound_snapshots_and_rom_state(self):
        p = self.float_compile('''def stop() -> i16:
    return int(float(input("Stop? ")))
def step() -> i16:
    return int(float(input("Step? ")))
data = b"A\\x00\\xffBCD"
print(data[:stop():step()], 1.5, data[::2])
''')
        for _ in range(2):
            p.keyboard = list(b'-7\r-2\r')
            p.run()
            self.assertEqual(p.output,
                             b"STOP? -7\rSTEP? -2\rB'DB\\X00' 1.5 B'A\\XFFC'\r")
            self.assertEqual(p.cpu.memory[0x0002:0x0090], [index ^ 0x55 for index in range(142)])
            self.assertEqual(p.cpu.memory[0x0300:0x0302], [0x3A, 0xA4])
            p.restart()
            p.output.clear()

    def test_bytes_slice_bound_only_calls_input_and_recursion_stack_guards(self):
        p = self.compile('data = b"\\x00\\x01\\x02\\x03\\x04\\x05\\x06"\n'
                         'result = data[:ord(input("Stop? ")) - 48:ord(input("Step? ")) - 48]\n'
                         'print(result)\n')
        p.keyboard = list(b'6\r2\r')
        p.run()
        self.assertEqual(p.output, b"STOP? 6\rSTEP? 2\rB'\\X00\\X02\\X04'\r")
        self.iteration_oracle('''def stop():
    return 4
def step():
    return -1
data = b"ABCDE"
print(data[:stop()], data[::step()])
''')
        self.reject('def cut(data: bytes) -> bytes:\n    return data[::step()]\n'
                    'def step() -> i16:\n    value = cut(b"x")\n    return len(value)\n'
                    'result = cut(b"abc")\n', 'recursion')
        definitions = 'def f0() -> i16:\n    return 1\n'
        definitions += ''.join(f'def f{i}() -> i16:\n    return f{i - 1}()\n' for i in range(1, 64))
        self.reject(definitions + 'data = b"abc"\nresult = data[::f63()]\n', 'stack')
        result, _, mapping = self.invoke('def unused(data: bytes) -> bytes:\n    return data[::-1]\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())

    def test_bytes_slice_arena_capacity_identity_guard_and_entry_reset(self):
        result, _, mapping = self.invoke('data = b"abc"\ncopy = data[:]\nother = data[0:None:1]\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())
        p = self.compile('data = ' + repr(b'x' * 8188) + '\n'
                         'saved = data[::2]\nsame = data[:]\nclipped = data[-32768:65535:1]\n'
                         'length = len(saved)\nlast = saved[-1]\n')
        for _ in range(2):
            p.run()
            self.assertEqual([p.value('length'), p.value('last')], [4094, 120])
            self.assertEqual(p.value('same'), p.value('data'))
            self.assertEqual(p.value('clipped'), p.value('data'))
            p.restart()
        source = '''def cut(data):
    return data[1:3]
data = b"\\x00\\xffAB"
saved = cut(data)
temporary = b""
for index in range(1023):
    temporary = cut(data)
print(saved, temporary)
'''
        p = self.compile(source)
        for _ in range(2):
            p.run()
            self.assertEqual(p.output, b"B'\\XFFA' B'\\XFFA'\r")
            p.restart()
            p.output.clear()
        p = self.compile(source.replace('1023', '1024'))
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        pointer = p.value('saved')
        self.assertEqual(bytes(p.cpu.memory[pointer:pointer + 2]), b'\xffA')
        for expression in ('data[:4095]', 'data[::-1]'):
            with self.subTest(expression=expression):
                p = self.compile('data = ' + repr(b'x' * 8188) + '\nresult = ' + expression + '\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()

    def test_bytes_slice_diagnostics_zero_step_and_immutable_assignment(self):
        for source, message in [('result = b"abc"[::0]\n', 'step cannot be zero'),
                                ('data = b"abc"\nresult = data[::False]\n', 'step cannot be zero'),
                                ('result = b"abc"[1.0:]\n', 'require integers'),
                                ('result = b"abc"[:"x"]\n', 'require integers'),
                                ('result = b"abc"[::1.5]\n', 'require integers'),
                                ('data = b"abc"\ndata[:] = b"x"\n', 'bytes are immutable'),
                                ('data = b"abc"\ndata[1:2] += b"x"\n', 'bytes are immutable'),
                                ('data = [1, 2]\nresult = data[:]\n', 'slicing currently requires bytes')]:
            with self.subTest(source=source):
                self.reject(source, message)
        for data in (b'', b'abc'):
            p = self.compile(f'data = {data!r}\nstep: i16 = 0\nresult = data[::step]\n')
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()

    def test_bytes_slicing_example_restart_and_saved_values(self):
        p = self.compile((ROOT / 'examples/bytes_slicing/main.py').read_text())
        for _ in range(2):
            p.run()
            self.assertEqual([p.value(name) for name in ('forward_length', 'forward_checksum',
                                                        'reverse_length', 'reverse_checksum')],
                             [3, 320, 7, 497])
            self.assertIn(b'FORWARD: 3 320\rREVERSE: 7 497\r', p.output)
            self.assertIn(b"SAVED: B'\\XFF*A\\X80'\r", p.output)
            p.restart()
            p.output.clear()

    def test_bytes_concatenation_repetition_literals_constants_and_imports(self):
        self.module('binary_repeat.py', '''from typing import Final
PREFIX: Final[bytes] = b"\\x00" + b"\\xff"
DATA: Final = PREFIX * 3
''')
        p = self.compile('''from binary_repeat import PREFIX, DATA
print(PREFIX, DATA, 2 * DATA, b"x" * -5, b"x" * False)
print(b"" * 65535, b"a" + b"b" + b"c", b"A" * True)
length = len(DATA)
''')
        p.run()
        self.assertEqual(p.value('length'), 6)
        self.assertIn(b"B'\\X00\\XFF\\X00\\XFF\\X00\\XFF'", p.output)
        self.assertTrue(p.output.endswith(b"B'' B'ABC' B'A'\r"))
        result, _, mapping = self.invoke('from typing import Final\nDATA: Final = b"a" * 3 + b"b"\na = DATA\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('constant DATA bytes length=4', mapping.read_text())
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())

    def test_bytes_runtime_concatenation_repetition_python_oracle(self):
        data = bytes(range(256))
        self.iteration_oracle(f'''data = {data!r}
tail = b"\\x00\\xff"
count: u16 = 3
joined = data + tail
repeated = count * data
print(joined, len(joined), joined[-1], repeated, len(repeated), repeated[-256])
empty = b""
negative: i16 = -32768
large: u16 = 65535
print(data * negative, negative * data, empty * large)
print(data * True, False * data, data + empty, empty + data)
''')
        self.iteration_oracle('''from enum import IntEnum
class Count(IntEnum):
    NONE = -1
    TWO = 2
source = b"\\x00\\xff"
print(source * Count.TWO, Count.TWO * source, source * Count.NONE)
small: i8 = -128
print(source * small)
''')

    def test_bytes_binary_nested_calls_operand_order_and_immutable_snapshots(self):
        self.iteration_oracle('''calls = [u16(0)]
def part(value):
    calls[0] = u16(calls[0] * 10 + value)
    return bytes([value, 0])
def count(value):
    calls[0] = u16(calls[0] * 10 + value)
    return value
def combine(a, b):
    return (a + b) * 2
first = part(1) + (part(2) + part(3))
print(first, calls[0])
calls[0] = 0
second = count(2) * part(3)
print(second, calls[0], first)
calls[0] = 0
third = part(1) * count(2)
print(third, calls[0], combine(first, second), first, second)
print(part(4) * count(0), calls[0])
''')

    def test_bytes_augmented_assignment_fields_properties_and_saved_aliases(self):
        self.iteration_oracle('''class Packet:
    _data: bytes = b"\\x00"
    calls: u16 = 0
    @property
    def data(self):
        self.calls = u16(self.calls * 10 + 1)
        return self._data
    @data.setter
    def data(self, value):
        self.calls = u16(self.calls * 10 + 3)
        self._data = value
def suffix(packet: Packet):
    packet.calls = u16(packet.calls * 10 + 2)
    packet._data = b"changed"
    return b"\\xff"
packet = Packet()
saved = packet._data
packet.data += suffix(packet)
print(saved, packet._data, packet.calls)
packet.calls = 0
packet.data *= 3
print(packet._data, packet.calls)
packet._data += b"A"
value = packet._data
old = value
value *= 2
value += b"end"
print(value, old, saved)
''')

    def test_bytes_binary_arena_limits_empty_identity_and_restart(self):
        p = self.compile('''count: u16 = 2047
source = b"\\x00\\xff"
result = source * count
length = len(result)
first = result[0]
last = result[-1]
empty = b""
one: u16 = 1
large: u16 = 65535
same = result * one
same = same + empty
same = empty + same
for index in range(1100):
    empty = empty * large
    same *= one
    same += empty
''')
        for _ in range(2):
            p.run(limit=3000000)
            self.assertEqual([p.value(name) for name in ('length', 'first', 'last')], [4094, 0, 255])
            self.assertEqual(p.value('same'), p.value('result'))
            p.restart()
        p = self.compile('left = ' + repr(b'a' * 2047) + '\nright = ' + repr(b'b' * 2047) +
                         '\nresult = left + right\nlength = len(result)\nfirst = result[0]\nlast = result[-1]\n')
        p.run()
        self.assertEqual([p.value(name) for name in ('length', 'first', 'last')], [4094, 97, 98])
        p = self.compile('data = ' + repr(b'x' * 8188) + '\none: u16 = 1\nempty = b""\n'
                         'same = data * one\nsame = same + empty\nlength = len(same)\n')
        p.run()
        self.assertEqual(p.value('length'), 8188)
        self.assertEqual(p.value('same'), p.value('data'))
        for source in ['count: u16 = 2048\ndata = b"ab"\nresult = count * data\n',
                       'count: u16 = 65535\ndata = b"x"\nresult = data * count\n',
                       'a = ' + repr(b'x' * 2050) + '\nb = a\nresult = a + b\n',
                       'count: u16 = 16\ndata = ' + repr(b'x' * 4096) + '\nresult = data * count\n',
                       'count: u16 = 2046\ndata = b"ab"\nsaved = data * count\nresult = data + data\n']:
            with self.subTest(source=source[:80]):
                p = self.compile(source + 'print("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertEqual(p.output, b'')
                if 'saved =' in source:
                    pointer = p.value('saved')
                    self.assertEqual(bytes(p.cpu.memory[pointer:pointer + 4092]), b'ab' * 2046)
        result, _, mapping = self.invoke('def unused(a: bytes, n: u16) -> bytes:\n    return a * n + a\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())

    def test_bytes_binary_operator_diagnostics_and_literal_bounds(self):
        for source in ['value = b"x" + "x"\n', 'value = "x" + b"x"\n',
                       'value = b"x" * 1.5\n', 'value = 1.5 * b"x"\n',
                       'value = b"x" * b"y"\n', 'value = b"x" + 1\n']:
            with self.subTest(source=source):
                self.reject(source, 'bytes concatenation requires')
        self.reject('value = b"x" * 32767\n', 'bytes repetition exceeds')
        self.reject('value = b"x" * 8191 + b"y" * 8191\n', 'bytes literal arena exhausted')
        # Static operand records and the folded result all occupy the literal pool.
        self.reject('value = ' + repr(b'x' * 8192) + ' + ' + repr(b'y' * 8192) + '\n',
                    'bytes literal arena exhausted')

    def test_bytes_operations_example_and_restart(self):
        p = self.compile((ROOT / 'examples/bytes_operations/main.py').read_text())
        for _ in range(2):
            p.run()
            self.assertEqual([p.value(name) for name in ('joined_length', 'joined_checksum',
                                                        'repeated_length', 'repeated_checksum',
                                                        'updated_length', 'updated_checksum')],
                             [4, 362, 12, 1086, 10, 980])
            self.assertIn(b'JOINED: 4 362\rREPEATED: 12 1086\rUPDATED: 10 980\r', p.output)
            p.restart()
            p.output.clear()

    def test_bytes_literals_all_values_indexing_truth_and_repr(self):
        data = bytes(range(256))
        p = self.iteration_oracle(f'''payload = {data!r}
empty = b""
zero = b"\\x00"
length = len(payload)
first = payload[0]
last = payload[-1]
print(payload, length, first, last)
print(bool(payload), bool(empty), bool(zero), not zero, ord(zero))
print(b"only'quote", b'only"quote', b"both'\\\"quote")
''')
        self.assertEqual(p.value('length'), 256)
        self.assertEqual(p.symbols['payload'][1:], (2, 'bytes'))
        pointer = p.value('payload')
        self.assertEqual(bytes(p.cpu.memory[pointer:pointer + 256]), data)
        self.assertEqual(p.cpu.memory[pointer - 2:pointer], [0, 1])

    def test_bytes_prefixes_raw_escapes_constants_and_modules(self):
        self.module('binary_data.py', r'''from typing import Final
DATA: Final[bytes] = B"\0\377\x2a"
RAW: Final = rB"\x41"
SIZE: Final = len(DATA)
''')
        p = self.compile('''from binary_data import DATA, RAW, SIZE
copy: bytes = DATA
raw: bytes = RAW
print(copy, raw, SIZE, DATA[-1])
''')
        p.run()
        self.assertEqual(p.output, b"B'\\X00\\XFF*' B'\\\\X41' 3 42\r")
        self.iteration_oracle(r'''one = br"\t\x00"
two = RB"\t\x00"
three = b"\a\b\f\v\r\t\n\1\12\123\x7f\xff"
print(one, two, three, one == two)
''')

    def test_bytes_runtime_comparisons_include_zeroes_and_lengths(self):
        values = [b'', b'\x00', b'\x00\x00', b'\x00\x01', b'A', b'\x7f', b'\x80', b'\xff']
        for left in values:
            for right in values:
                with self.subTest(left=left, right=right):
                    self.iteration_oracle(f'a = {left!r}\nb = {right!r}\n'
                                          'print(a == b, a != b, a < b, a <= b, a > b, a >= b)\n')
        self.iteration_oracle('''a = b"ASCII"
b = "ASCII"
print(a == b, a != b, a == 65, 65 != a)
print((a or b"fallback"), (b"" and a), (a and b"other"))
''')

    def test_bytes_functions_fields_properties_and_list_elements(self):
        self.iteration_oracle(r'''class Packet:
    _data: bytes = b"\x00\xff"
    @property
    def data(self) -> bytes:
        return self._data
    @data.setter
    def data(self, value: bytes):
        self._data = value
def identity(value):
    return value
def choose(flag):
    if flag:
        return b"yes"
    return b"no"
def update(values: list[bytes]):
    values[1] = choose(True)
packet = Packet()
items = [b"first", packet.data]
update(items)
saved = identity(packet.data)
packet.data = items[1]
print(saved, packet.data, items[0], items[1], packet.data[-1])
''')

    def test_bytes_iteration_captures_immutable_values_and_loop_else(self):
        self.iteration_oracle(r'''source = b"\x00\xffA"
total: u16 = 0
for value in source:
    source = b"changed"
    total += value
    print(value)
else:
    print("done")
for value in b"\x00\x01":
    if value == 0:
        continue
    for nested in b"AB":
        print(value, nested)
        break
    else:
        print("wrong")
print(source, total)
''')

    def test_bytes_constructors_counts_lists_text_and_immutable_snapshots(self):
        self.iteration_oracle(r'''def capture(values):
    return bytes(values)
values = [u16(0), u16(255), u16(42)]
first = capture(values)
values[1] = 65
second = capture(values)
count: u16 = 300
zeroes = bytes(count)
text = "Ascii\nText"
encoded = bytes(text, "UTF_8")
copy = bytes(encoded)
print(first, second, len(zeroes), zeroes[-1], bool(zeroes))
print(encoded, copy, bytes(), bytes(3), bytes([0, True, 255]))
''')
        self.iteration_oracle('''def make(a, b):
    return bytes([a, b, True])
first = make(0, 255)
second = make(42, 65)
print(first, second)
''')

    def test_bytes_constructor_signed_element_guards_and_count_limits(self):
        for source in ['values = [i8(-1)]\nresult = bytes(values)\n',
                       'values = [i16(-1)]\nresult = bytes(values)\n',
                       'values = [u16(256)]\nresult = bytes(values)\n',
                       'count: i16 = -1\nresult = bytes(count)\n',
                       'count: u16 = 4095\nresult = bytes(count)\n']:
            with self.subTest(source=source):
                p = self.compile(source + 'print("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertEqual(p.output, b'')
        self.iteration_oracle('values = [i8(0), i8(127)]\nresult = bytes(values)\nprint(result)\n')
        p = self.compile('count: u16 = 4094\nresult = bytes(count)\nlength = len(result)\nlast = result[-1]\n')
        p.run()
        self.assertEqual(p.value('length'), 4094)
        self.assertEqual(p.value('last'), 0)
        p.restart()
        p.run()
        self.assertEqual(p.value('length'), 4094)

    def test_bytes_immutable_index_and_constructor_diagnostics(self):
        for source, message in [
            ('value = b"x"\nvalue[0] = 1\n', 'bytes are immutable'),
            ('value = b"x"\nindex = 0\nvalue[index] = 1\n', 'bytes are immutable'),
            ('value: str = b"x"\n', 'cannot assign bytes'),
            ('value = bytes("text")\n', 'encoding'),
            ('value = bytes("text", "utf-16")\n', 'encoding'),
            ('value = bytes([256])\n', '0..255'),
            ('value = bytes([-1])\n', '0..255'),
            ('value = bytes([1.5])\n', 'require integers'),
            ('value = bytes(1.0)\n', 'requires a count'),
            ('value = bytes(1, 2, 3)\n', 'bytes expects'),
            ('bytes = 1\n', 'reserved compiler builtin'),
            ('value = b"\\x0g"\n', 'two hex digits'),
            ('value = b"\\777"\n', '0..255'),
            ('value = b"x"[1]\n', 'out of bounds'),
            ('value = ord(b"")\n', 'one-character'),
        ]:
            with self.subTest(source=source):
                if '\\777' in source:
                    result, _, _ = self.invoke(source, bindings=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(message, result.stderr)
                else:
                    self.reject(source, message)
        p = self.compile('value = b"x"\nindex: i16 = -2\nresult = value[index]\n')
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()

    def test_bytes_array_references_enum_values_and_evaluation_order(self):
        self.iteration_oracle('''from enum import IntEnum
class Value(IntEnum):
    ZERO = 0
    HIGH = 255
values = array(Value, 3)
values[1] = Value.HIGH
def capture(values: array[Value]):
    return bytes(values)
saved = capture(values)
values[1] = Value.ZERO
print(saved, capture(values))
counter = array(u8, 1)
def next_byte():
    counter[0] = u8(counter[0] + 1)
    return counter[0]
result = bytes([next_byte(), next_byte(), next_byte()])
print(result, counter[0])
data = b"AB"
def change() -> i16:
    data = b"CD"
    return -1
print(data[change()])
class Packet:
    data: bytes = b"AB"
def replace(packet: Packet) -> i16:
    packet.data = b"CD"
    return -1
packet = Packet()
print(packet.data[replace(packet)], packet.data)
''')
        self.iteration_oracle('''def capture(values: array[i8]):
    return bytes(values)
values = array(i8, 3)
values[1] = 127
print(capture(values))
''')

    def test_bytes_arena_exhaustion_saved_values_and_entry_reset(self):
        source = '''def capture(values):
    return bytes(values)
values = [u8(1), u8(2)]
saved = capture(values)
temporary = bytes()
for index in range(1000):
    temporary = capture(values)
print(saved, temporary)
'''
        p = self.compile(source)
        p.run()
        self.assertEqual(p.output, b"B'\\X01\\X02' B'\\X01\\X02'\r")
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.output, b"B'\\X01\\X02' B'\\X01\\X02'\r")
        p = self.compile(source.replace('range(1000)', 'range(1024)'))
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        self.assertEqual(p.output, b'')
        result, _, mapping = self.invoke('def unused(count: u16) -> bytes:\n    return bytes(count)\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime immutable_bytes ', mapping.read_text())

    def test_bytes_literal_arena_deduplication_and_size_limit(self):
        result, _, mapping = self.invoke('from typing import Final\n'
                                        'DATA: Final[bytes] = b"\\x00\\xff"\n'
                                        'COPY: Final = DATA\na = DATA\nb = b"\\x00\\xff"\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('constant DATA bytes length=2', mapping.read_text())
        p = self.compile('a = b"same\\x00"\nb = b"same\\x00"\n')
        p.run()
        self.assertEqual(p.value('a'), p.value('b'))
        result, _, _ = self.invoke('value = ' + repr(b'x' * 32764) + '\nlength = len(value)\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.reject('value = ' + repr(b'x' * 32765) + '\n', 'bytes literal arena exhausted')

    def test_bytes_disk_write_from_preserves_binary_data(self):
        p = self.compile('''import c64.disk as disk
data = b"\\x00\\xffAb\\n\\r\\x80"
opened = disk.open(2, 8, 2, "binary,s,w")
written = disk.write_from(2, data, len(data))
disk.close(2)
print(data, written)
''')
        p.disk = DiskKernal()
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.disk.files[b'BINARY'], b'\0\xffAb\n\r\x80')
        self.assertEqual(p.value('written'), 7)
        self.assertEqual(p.output, b"B'\\X00\\XFFAB\\N\\R\\X80' 7\r")
        self.reject('import c64.disk as disk\ndata = b"x"\ndisk.read_into(2, data, 1)\n',
                    'bytes are immutable', diagnostic='disk.py')

    def test_bytes_example_disk_roundtrip_and_file_exists_preservation(self):
        p = self.compile((ROOT / 'examples/bytes/main.py').read_text())
        p.disk = DiskKernal()
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.disk.files[b'PYC64-BYTES'], b'\x00\xff\x2a')
        self.assertEqual([p.value(name) for name in ('length', 'checksum', 'written', 'received', 'dos')],
                         [3, 297, 3, 3, 0])
        self.assertIn(b"BYTES: B'\\X00\\XFF*' 3 297\r", p.output)
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.disk.files[b'PYC64-BYTES'], b'\x00\xff\x2a')
        self.assertEqual([p.value(name) for name in ('length', 'checksum', 'written', 'received', 'dos')],
                         [3, 297, 0, 0, 63])
        self.assertIn(b"BYTES: B'\\X00\\XFF*' 3 297\r", p.output)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_bytes_mixed_float_print_argument_snapshots(self):
        p = self.float_compile('''def make(value) -> bytes:
    return bytes([value, 255])
def real() -> float:
    return 1.25
print(make(1), real(), make(2), make(3))
''')
        p.run()
        self.assertEqual(p.output, b"B'\\X01\\XFF' 1.25 B'\\X02\\XFF' B'\\X03\\XFF'\r")

    @staticmethod
    def target_float(value):
        """Round the oracle to the documented C64 binary precision/range."""
        if value == 0:
            return 0.0
        mantissa, exponent = math.frexp(abs(value))
        if exponent < -127:
            return 0.0
        significand = int(mantissa * 2**32 + 0.5)
        if significand == 2**32:
            significand >>= 1
            exponent += 1
        if exponent > 127:
            raise OverflowError('C64 finite range')
        return math.copysign(math.ldexp(significand, exponent - 32), value)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_divmod_signs_mixed_operands_and_integral_float_results(self):
        source = []
        cases = [(a, b) for a in [7.5, -7.5, 0.0, 8.0, -8.0]
                 for b in [2.0, -2.0, 0.5, -0.5]]
        for index, (a, b) in enumerate(cases):
            source += [f'a{index} = {a!r}', f'b{index} = {b!r}',
                       f'q{index} = a{index} // b{index}', f'r{index} = a{index} % b{index}']
        source += ['mixed = 7 // 2.0', 'signed = -7 % 2.0',
                   'other = 7.5 % 2', 'truth = True // 0.5']
        p = self.float_compile('\n'.join(source) + '\n')
        p.run()
        for index, (a, b) in enumerate(cases):
            self.assertEqual(p.value(f'q{index}'), a // b)
            self.assertEqual(p.value(f'r{index}'), a % b)
            self.assertEqual(p.symbols[f'q{index}'][1:], (5, 'float'))
        self.assertEqual([p.value(name) for name in ['mixed', 'signed', 'other', 'truth']],
                         [3.0, 1.0, 1.5, 2.0])

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_divmod_decimal_boundaries_and_underflow(self):
        pairs = [(1.0, 0.1), (0.6, 0.2), (1.0, 0.3), (10.0, 0.1),
                 (1.0000000004, 1.0), (0.9999999996, 1.0),
                 (2.0**-128 * 1.875, 2.0**-128),
                 (2.0**-128, 2.0**-128 * 1.875),
                 (2.0**31 - 1, 0.5), (2.0**32 + 3, 1.0),
                 (2.0**100, 3.0), (1.0, 2.0**126)]
        for left, right in pairs:
            for a_sign, b_sign in [(1, 1), (-1, 1), (1, -1), (-1, -1)]:
                a, b = left * a_sign, right * b_sign
                with self.subTest(a=a, b=b):
                    p = self.float_compile(f'a = {a!r}\nb = {b!r}\nq = a // b\nr = a % b\n')
                    p.run()
                    x, y = p.value('a'), p.value('b')
                    self.assertEqual(p.value('q'), self.target_float(x // y))
                    self.assertEqual(p.value('r'), self.target_float(x % y))

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_divmod_random_packed_values_across_exponent_range(self):
        rng = random.Random(81462)
        for batch in range(6):
            source = []
            for index in range(16):
                a = math.ldexp(1 + rng.randrange(2**31) / 2**31, rng.randrange(-128, 127))
                b = math.ldexp(1 + rng.randrange(2**31) / 2**31, rng.randrange(-128, 127))
                a *= rng.choice([-1, 1])
                b *= rng.choice([-1, 1])
                source += [f'a{index} = {a!r}', f'b{index} = {b!r}',
                           f'r{index} = a{index} % b{index}']
                try:
                    self.target_float(a // b)
                except OverflowError:
                    continue
                source += [f'q{index} = a{index} // b{index}']
            p = self.float_compile('\n'.join(source) + '\n')
            p.run()
            for index in range(16):
                with self.subTest(batch=batch, index=index):
                    a, b = p.value(f'a{index}'), p.value(f'b{index}')
                    self.assertEqual(p.value(f'r{index}'), self.target_float(a % b))
                    if f'q{index}' in p.symbols:
                        self.assertEqual(p.value(f'q{index}'), self.target_float(a // b))

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_divmod_argument_snapshots_and_function_inference(self):
        p = self.float_compile('''values = array(float, 1)
calls = array(u8, 1)
values[0] = 7.5
def left():
    calls[0] = u8(calls[0] + 1)
    return values[0]
def right():
    calls[0] = u8(calls[0] + 1)
    values[0] = 100.0
    return -2.0
def divide(a, b):
    return a // b
def remainder(a, b):
    return a % b
first = divide(left(), right())
values[0] = 7.5
second = left() % right()
third = remainder(divide(17.0, 2.0), 3.0)
print(first, second, third, calls[0])
''')
        p.run()
        self.assertEqual(p.output, b'-4.0 -0.5 2.0 4\r')
        self.assertEqual(p.symbols['divide.a'][1:], (5, 'float'))
        self.assertEqual(p.cpu.memory[0x0002:0x0090], [index ^ 0x55 for index in range(142)])
        self.assertEqual(p.cpu.memory[0x0300:0x0302], [0x3A, 0xA4])

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_divmod_augmented_variables_fields_properties_and_integer_forms(self):
        p = self.float_compile('''class Number:
    _value: float = 7.5
    _count: u8 = 0
    @property
    def value(self) -> float:
        self._count += 1
        return self._value
    @value.setter
    def value(self, value: float):
        self._value = value
def divisor(value: Number) -> float:
    value._count += 10
    value._value = 100.0
    return -2.0
number = Number()
number.value //= divisor(number)
first = number._value
number._value = 7.5
number.value %= divisor(number)
second = number._value
number._value /= 2.0
third = number._value
value = 7.5
value //= -2.0
value %= 3.0
value /= 4.0
integer: i16 = -7
integer //= 2
integer %= 3
print(first, second, third, value, integer, number._count)
''')
        p.run()
        self.assertEqual(p.output, b'-4.0 -0.5 -0.25 0.5 2 22\r')

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_divmod_zero_divisors_and_quotient_overflow_trap(self):
        for operation in ['//', '%', '//=', '%=', '/=']:
            with self.subTest(operation=operation):
                source = 'value = 7.5\nzero = 0.0\n'
                source += (f'value {operation} zero\n' if operation.endswith('=')
                           else f'result = value {operation} zero\n')
                p = self.float_compile(source + 'print("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertEqual(p.output, b'')
        for a, b in [(1e38, 1e-38), (-1e38, 1e-38), (1e38, -1e-38)]:
            with self.subTest(a=a, b=b):
                p = self.float_compile(f'a = {a!r}\nb = {b!r}\nresult = a // b\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                p = self.float_compile(f'a = {a!r}\nb = {b!r}\nresult = a % b\n')
                p.run()
                self.assertEqual(p.value('result'), self.target_float(p.value('a') % p.value('b')))

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_divmod_example_output_and_restart(self):
        p = self.float_compile((ROOT / 'examples/float_divmod/main.py').read_text())
        p.clobber_kernal = True
        p.run()
        expected = (b'FLOOR: -4.0\rREMAINDER: 0.5\rDECIMAL: 9.0\rHUGE MODULO: '
                    + str(self.target_float(1e38) % 3.0).encode()
                    + b'\rUPDATED: 0.5\r')
        self.assertEqual(p.output, expected)
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.output, expected)
        self.assertEqual(p.cpu.memory[0x0002:0x0090], [index ^ 0x55 for index in range(142)])

    def test_float_divmod_native_kernel_pruning_and_augmented_diagnostics(self):
        # Native exact division runs without installing a ROM or arithmetic stub.
        p = self.compile('a = 1e38\nb = 3.0\nresult = a % b\nsmall = 7.5 // 2.0\n')
        p.run()
        self.assertEqual(p.value('small'), 3.0)
        self.assertEqual(p.value('result'), self.target_float(p.value('a') % 3.0))
        result, _, mapping = self.invoke('def unused(value: float) -> float:\n    return value % 2.0\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime float_divmod ', mapping.read_text())
        self.reject('value = 1.0\nvalue %= "text"\n', 'incompatible inferred types')
        self.reject('value = 1\nvalue /= 2\n', 'cannot assign float')
        self.reject('values = [1.0]\nvalues[0] %= 2.0\n', 'augmented array assignment')
        self.reject('value: float //= 2.0\n', 'cannot declare a type')

    def test_intenum_members_auto_aliases_and_iteration_match_python(self):
        p = self.iteration_oracle('''from enum import IntEnum, auto
from typing import Final
BASE: Final = 5
class Mode(IntEnum):
    OFF = 0
    RUN = BASE
    RUNNING = RUN
    WAIT = auto()
    LOW = 2
    NEXT = auto()
current: Mode = Mode.RUNNING
print(current, current.name, current.value, Mode.RUNNING.name, len(Mode))
print(Mode["RUNNING"].name, Mode(6).name)
for member in Mode:
    print(member.name, member.value)
else:
    print("done")
''')
        self.assertEqual(p.value('current'), 5)
        self.assertEqual(p.symbols['current'][1:], (1, 'u8'))
        expected = bytes(p.output)
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.output, expected)

    def test_intenum_runtime_lookups_capture_values_once(self):
        self.iteration_oracle('''import enum as e
class Direction(e.IntEnum):
    LEFT = -300
    RIGHT = 400
    EAST = RIGHT
calls = array(u8, 1)
def number():
    calls[0] = u8(calls[0] + 1)
    return i16(-300)
def word():
    calls[0] = u8(calls[0] + 1)
    return "EAST"
value = Direction(number())
alias = Direction[word()]
print(value.name, value.value, alias.name, alias.value, calls[0])
print(Direction(number()).name, Direction[word()].value, calls[0])
''')

    def test_intenum_functions_fields_properties_and_container_references(self):
        self.iteration_oracle('''from enum import IntEnum
class State(IntEnum):
    IDLE = 0
    ACTIVE = 300
class Device:
    _state: State = State.IDLE
    @property
    def state(self) -> State:
        return self._state
    @state.setter
    def state(self, value: State):
        self._state = value
def choose(flag):
    if flag:
        return State.ACTIVE
    return State.IDLE
def identity(value):
    return value
def fill(values: list[State], value: State):
    for index in range(len(values)):
        values[index] = value
def write(values: array[State]):
    values[1] = State.ACTIVE
device = Device()
device.state = choose(True)
items: list[State] = [State.IDLE, State.ACTIVE]
fill(items, identity(State.ACTIVE))
buffer = array(State, 2)
write(buffer)
print(device.state.name, device._state.name, items[0].name, buffer[1].name)
print(identity(items[1]).value, buffer[0].name)
''')

    def test_intenum_integer_operations_lose_nominal_type(self):
        self.iteration_oracle('''from enum import IntEnum
class Bits(IntEnum):
    ZERO = 0
    LOW = 2
    HIGH = 8
left = Bits.LOW
right = Bits.HIGH
def add(a, b):
    return a + b
def folded():
    return Bits.HIGH ** 3
number = add(left, right)
power = folded()
print(number, left | right, left << 1, int(left), +left, -left, left == 2)
print(power, (left or right).name, bool(Bits.ZERO))
''')

    def test_intenum_signed_and_unsigned_layout_boundaries(self):
        for values, kind, size in [((-128, 127), 'i8', 1),
                                  ((-32768, 32767), 'i16', 2),
                                  ((0, 255), 'u8', 1),
                                  ((0, 65535), 'u16', 2)]:
            with self.subTest(values=values):
                low, high = values
                p = self.iteration_oracle(f'''from enum import IntEnum
class Limits(IntEnum):
    LOW = {low}
    HIGH = {high}
first = Limits.LOW
second = Limits.HIGH
word = second.value
again = Limits(word)
print(first, first.name, second, again.name)
''')
                self.assertEqual(p.symbols['first'][1:], (size, kind))
                self.assertEqual(p.value('first'), low)
                self.assertEqual(p.value('second'), high)

    def test_intenum_imports_aliases_reexports_and_constants(self):
        self.module('colors.py', '''from enum import IntEnum as Base, auto as next_value
from typing import Final
START: Final = 7
class Color(Base):
    RED = START
    GREEN = next_value()
    BLUE = next_value()
DEFAULT: Final[Color] = Color.GREEN
def pick(value: Color) -> Color:
    return value
''')
        self.module('palette.py', 'from colors import Color as Shade, DEFAULT, pick\n')
        p = self.compile('''from palette import Shade as Tone, DEFAULT, pick
import colors as original
current: Tone = pick(DEFAULT)
label = "BLUE"
blue = Tone[label]
print(current.name, blue.name, original.Color.RED.value)
''')
        p.run()
        self.assertEqual(p.output, b'GREEN BLUE 7\r')
        self.assertEqual(p.value('current'), 8)

    def test_intenum_runtime_invalid_values_names_and_signedness_trap(self):
        definitions = '''from enum import IntEnum
class Signed(IntEnum):
    NEGATIVE = -1
    POSITIVE = 1
class Unsigned(IntEnum):
    LOW = 0
    HIGH = 65535
'''
        for source in ['value: i16 = 2\nresult = Signed(value)\n',
                       'value: u16 = 65535\nresult = Signed(value)\n',
                       'value: i16 = -1\nresult = Unsigned(value)\n',
                       'name = "MISSING"\nresult = Signed[name]\n',
                       'name = "negative"\nresult = Signed[name]\n']:
            with self.subTest(source=source):
                p = self.compile(definitions + source + 'print("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertEqual(p.output, b'')

    def test_intenum_diagnostics_and_nominal_assignments(self):
        definitions = '''from enum import IntEnum, auto
class A(IntEnum):
    ZERO = 0
    ONE = 1
class B(IntEnum):
    ZERO = 0
    ONE = 1
'''
        for source, message in [
            ('value: A = 1\n', "requires a member of IntEnum 'A'"),
            ('value: A = B.ONE\n', "requires a member of IntEnum 'A'"),
            ('value = A(2)\n', 'not a member'),
            ('value = A(1.5)\n', 'requires an integer'),
            ('value = A()\n', 'expects one integer'),
            ('value = A[1]\n', 'requires a string'),
            ('value = A["MISSING"]\n', 'unknown IntEnum'),
            ('A.ONE = 1\n', 'read-only'),
            ('value = A.ONE\nvalue.name = "new"\n', 'invalid assignment target'),
            ('value = A.ONE\nvalue += 1\n', "requires a member of IntEnum 'A'"),
            ('values = [A.ONE, B.ONE]\n', 'same IntEnum'),
            ('value = A.ONE\nfor value in range(2):\n    pass\n', 'loop variable'),
            ('A = 1\n', 'conflicts with class'),
            ('def take(value: A):\n    return value\nresult = take(B.ONE)\n', 'requires a member'),
        ]:
            with self.subTest(source=source):
                self.reject(definitions + source, message)
        for source, message in [
            ('from enum import Enum\n', 'only enum.IntEnum'),
            ('from enum import IntEnum\nclass Empty(IntEnum):\n    pass\n', 'at least one'),
            ('from enum import IntEnum\nclass A(IntEnum):\n    X = 1\n    X = 2\n', 'duplicate'),
            ('from enum import IntEnum\nclass A(IntEnum):\n    X = "one"\n', 'compile-time integer'),
            ('from enum import IntEnum\nclass A(IntEnum):\n    X = -1\n    Y = 65535\n', 'signedness'),
            ('from enum import IntEnum\nclass A(IntEnum):\n    X = Y\n    Y = 1\n', 'earlier member'),
            ('from enum import IntEnum, auto\nclass A(IntEnum):\n    X = 65535\n    Y = auto()\n', '16-bit'),
            ('from enum import IntEnum\nx = A.X\nclass A(IntEnum):\n    X = 1\n', 'before its class'),
            ('from enum import IntEnum\nclass A(IntEnum):\n    X = 1\nvalues = array(A, 2)\n', 'zero member'),
        ]:
            with self.subTest(source=source):
                self.reject(source, message)

    def test_intenum_array_host_adapter_and_source_definition_restrictions(self):
        source = '''from enum import IntEnum
from pyc64.types import array
class State(IntEnum):
    OFF = 0
    ON = 1
values = array(State, 2)
values[1] = State.ON
print(values[0].name, values[1].name)
'''
        self.iteration_oracle(source)
        for body in ['X: int = 1', 'def method(self):\n        return 1',
                     '_ignore_ = "X"', 'name = 1']:
            with self.subTest(body=body):
                self.reject('from enum import IntEnum\nclass A(IntEnum):\n    ' + body + '\n',
                            'IntEnum')

    def test_intenum_constant_expressions_nominal_buffer_checks_and_pruning(self):
        definitions = '''from enum import IntEnum, auto
from typing import Final
BASE: Final = 4
class Mode(IntEnum):
    OFF = 0
    ON = (BASE << 1) | 1
    ALIAS = ON
    NEXT = auto()
    SHORT = 0 and unknown
class Other(IntEnum):
    OFF = 0
    ON = 9
'''
        result, _, mapping = self.invoke(definitions + '''def unused() -> str:
    return Mode.ON.name
current = Mode.ON
print(current)
''')
        self.assertEqual(result.returncode, 0, result.stderr)
        layout = mapping.read_text()
        self.assertIn('enum Mode u8 members=5', layout)
        self.assertIn('enum_member Mode.ALIAS 9 canonical=ON', layout)
        self.assertNotIn('function unused ', layout)
        self.assertNotIn('runtime float', layout)
        for source in ['def consume(values: list[Mode]):\n    values[0] = Mode.ON\n'
                       'values = [Other.OFF]\nconsume(values)\n',
                       'def consume(values: array[Mode]):\n    values[0] = Mode.ON\n'
                       'values = array(Other, 2)\nconsume(values)\n']:
            with self.subTest(source=source):
                self.reject(definitions + source, 'element type')
        self.reject('from enum import IntEnum, auto\nclass A(IntEnum):\n    X = auto() + 1\n',
                    'complete member initializer')
        self.iteration_oracle(definitions + 'Mode(9)\nlen(Mode)\nprint(Mode.NEXT.name)\n')
        self.iteration_oracle(definitions + '''class Derived(IntEnum):
    BASE = Mode.ON.value
    NEXT = BASE + 1
value = Derived.NEXT
print(value.name, value.value)
''')

    def test_intenum_bounded_metadata_arenas(self):
        members = ''.join(f'    M{index} = {index}\n' for index in range(256))
        result, _, _ = self.invoke('from enum import IntEnum\nclass Values(IntEnum):\n' + members + 'print(Values.M255)\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.reject('from enum import IntEnum\nclass Values(IntEnum):\n' + members + '    EXTRA = 256\n',
                    'enum member arena exhausted')
        enums = ''.join(f'class E{index}(IntEnum):\n    ONLY = {index}\n' for index in range(32))
        result, _, _ = self.invoke('from enum import IntEnum\n' + enums + 'print(E31.ONLY)\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.reject('from enum import IntEnum\n' + enums + 'class Extra(IntEnum):\n    X = 0\n',
                    'enum arena exhausted')

    def test_intenum_example_native_execution_and_restart(self):
        p = self.iteration_oracle((ROOT / 'examples/enums/main.py').read_text())
        expected = bytes(p.output)
        self.assertEqual(p.value('current'), 2)
        self.assertEqual(p.value('selected'), 2)
        self.assertEqual(p.value('total'), 6)
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.output, expected)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_intenum_float_promotion_preserves_unsigned_values(self):
        p = self.float_compile('''from enum import IntEnum
class Word(IntEnum):
    ZERO = 0
    MAX = 65535
current = Word.MAX
decimal = float(current)
promoted = current + 0.5
product = 0.5 * current
print(current.name, decimal)
''')
        p.run()
        self.assertEqual(p.value('decimal'), 65535.0)
        self.assertEqual(p.value('promoted'), 65535.5)
        self.assertEqual(p.value('product'), 32767.5)
        self.assertEqual(p.output, b'MAX 65535.0\r')

    def float_compile(self, source):
        rom = os.environ.get('PYC64_BASIC_ROM')
        if not rom:
            self.skipTest('configure --basic_rom or PYC64_BASIC_ROM for C64 float CPU checks')
        data = Path(rom).read_bytes()
        self.assertEqual(len(data), 8192, 'C64 BASIC ROM must contain 8192 bytes')
        p = self.compile(source)
        p.cpu.memory[0xA000:0xC000] = data
        if KERNAL_ROM:
            kernal = Path(KERNAL_ROM).read_bytes()
            self.assertEqual(len(kernal), 8192, 'C64 KERNAL ROM must contain 8192 bytes')
            p.cpu.memory[0xE000:0x10000] = kernal
        p.cpu.memory[0x0002:0x0090] = [index ^ 0x55 for index in range(142)]
        p.cpu.memory[0x0300:0x0302] = [0x3A, 0xA4]
        return p

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_arithmetic_comparisons_conversions_and_restart(self):
        p = self.float_compile('''a = 1.25
b: float = 2.5
total = a + b
difference = b - a
product = a * b
quotient = b / a
mixed = a + 500
negative = -a
integer_unsigned: u16 = 65535
integer_signed: i16 = -32768
unsigned = float(integer_unsigned)
signed = float(integer_signed)
zero = float()
print(total, difference, product, quotient, mixed, negative, unsigned, signed, zero)
print(a < b, a <= b, a == b, a != b, a > b, a >= b, b >= b)
print(bool(a), bool(zero), not a, not zero)
''')
        before = p.cpu.memory[2:0x90]
        for _ in range(2):
            p.output.clear()
            p.clobber_kernal = True
            p.run()
            self.assertEqual(p.output, b'3.75 1.25 3.125 2.0 501.25 -1.25 65535.0 -32768.0 0.0\r'
                                      b'TRUE TRUE FALSE TRUE FALSE FALSE TRUE\rTRUE FALSE FALSE TRUE\r')
            self.assertEqual(p.value('quotient'), 2.0)
            self.assertEqual(p.value('unsigned'), 65535.0)
            self.assertEqual(p.cpu.memory[2:0x90], before)
            self.assertEqual(p.cpu.memory[0x0300:0x0302], [0x3A, 0xA4])
            self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
            self.assertEqual(p.cpu.sp, 0xF0)
            p.restart()

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_string_conversion_python_values(self):
        for value in ['0', '134', '-2.5', ' +.5 ', '1.', '1e2', '1E-2',
                      '1_234.5', '\t-12.5\n', '1.2_5e+2', '65535']:
            with self.subTest(value=value):
                p = self.float_compile('text = ' + repr(value) + '\nresult = float(text)\n')
                p.run()
                self.assertAlmostEqual(p.value('result'), float(value), delta=max(1e-10, abs(float(value)) * 1e-8))

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_function_argument_and_print_snapshots(self):
        p = self.float_compile('''def twice(value):
    return value * 2
def add(first, second):
    return first + second
first = twice(1.25)
second = twice(300)
print(first, second, add(twice(1.5), twice(2.5)), twice(4.5))
print(twice(1.5) + twice(2.5), twice(5.0), twice(6.0))
''')
        p.run()
        self.assertEqual(p.output, b'2.5 600.0 8.0 9.0\r8.0 10.0 12.0\r')
        self.assertEqual(p.symbols['twice.value'][1:], (5, 'float'))
        self.assertEqual(p.value('first'), 2.5)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_class_properties_lists_and_references(self):
        p = self.float_compile('''class P:
    _value: float = 1.5
    @property
    def value(self):
        return self._value
    @value.setter
    def value(self, value: float):
        self._value = value

def change(values: list[float]):
    values[-1] = values[0] / 2

p = P()
p.value += 2.5
values = [p.value, 2, 3.5]
change(values)
for value in values:
    print(value)
print(p.value)
''')
        p.run()
        self.assertEqual(p.output, b'4.0\r2.0\r2.0\r4.0\r')
        self.assertEqual(p.value('p._value'), 4.0)
        self.assertEqual(p.symbols['values'][1], 15)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_invalid_strings_division_and_overflow_guards(self):
        for value in ['', ' ', '.', '1e', '--1', '1.2.3', '12x', '1 2', '1__2',
                      '1._2', '1_', '_1', 'inf', 'nan', '1e39']:
            with self.subTest(value=value):
                p = self.float_compile('result = float(' + repr(value) + ')\nprint("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertNotIn(b'UNREACHABLE', p.output)
        for source in ['denominator = 0.0\nresult = 1 / denominator\n',
                       'large = 1e38\nresult = large * 10\n']:
            p = self.float_compile(source)
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_input_kevinallen_program(self):
        p = self.float_compile('''def kevinallen(peanut_butter, grouper_sandwich):
    winston = peanut_butter * 0 + 134
    hohoho = grouper_sandwich * 0 + 2
    diabetes = winston / hohoho
    return diabetes

ccms = float(input("What is your SSN? "))
lime = float(input("What are your credit card numbers? "))
print(kevinallen(ccms, lime))
''')
        p.keyboard = list(b'123\r456\r')
        p.run()
        self.assertEqual(p.output, b'WHAT IS YOUR SSN? 123\rWHAT ARE YOUR CREDIT CARD NUMBERS? 456\r67.0\r')
        self.assertEqual(p.value('ccms'), 123.0)
        self.assertEqual(p.value('lime'), 456.0)

    def test_float_compile_diagnostics_and_literal_forms(self):
        for value in ['.5', '5.', '1e2', '1_000.5', '1.2_5e+2']:
            result, _, _ = self.invoke('value = ' + value + '\n')
            self.assertEqual(result.returncode, 0, result.stderr)
        for source, message in [('value = 1e39\n', 'finite C64 range'),
                                ('value = 1.2__3\n', 'underscore'),
                                ('value = 1e+\n', 'invalid float'),
                                ('value = 1.5 & 1\n', 'unsupported float'),
                                ('float("a", "b")\n', 'expects 1'),
                                ('float([1])\n', 'float requires')]:
            with self.subTest(source=source):
                self.reject(source, message)
        definitions = 'def f0() -> float:\n    return float(input())\n'
        definitions += ''.join(f'def f{i}() -> float:\n    return f{i-1}()\n' for i in range(1, 50))
        self.reject(definitions + 'print(f49())\n', 'stack')

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_mixed_arithmetic_python_oracle(self):
        rng = random.Random(40)
        pairs = [(-12.5, 2.5), (0.5, -2.0), (1e20, 1e-10), (1e-20, 1e10)]
        pairs += [(rng.uniform(-1000, 1000), rng.uniform(1, 1000)) for _ in range(8)]
        source = []
        expected = {}
        for index, (first, second) in enumerate(pairs):
            source += [f'a{index} = {first!r}', f'b{index} = {second!r}']
            for suffix, operator in [('sum', '+'), ('difference', '-'), ('product', '*'), ('quotient', '/')]:
                name = f'{suffix}{index}'
                source.append(f'{name} = a{index} {operator} b{index}')
                expected[name] = eval(f'first {operator} second')
        source += ['integer_division = 5 / 2', 'positive = 0.0 or 1.5',
                   'negative = -2.5 and -1.5']
        expected.update(integer_division=2.5, positive=1.5, negative=-1.5)
        p = self.float_compile('\n'.join(source) + '\n')
        p.run()
        for name, value in expected.items():
            self.assertAlmostEqual(p.value(name), value, delta=max(abs(value) * 1e-7, 1e-38))

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for float execution checks')
    def test_float_array_layout_and_index_evaluation_order(self):
        p = self.float_compile('''values = array(float, 70)
def value():
    print("value")
    return 1.25
def index():
    print("index")
    return 69
values[index()] = value()
def update(values: array[float]):
    values[69] = values[69] * 2
update(values)
last = values[69]
print(last, values[0])
current: float = 0.0
for current in [1, 2]:
    print(current)
''')
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'VALUE\rINDEX\r2.5 0.0\r1.0\r2.0\r')
        self.assertEqual(p.symbols['values'][1], 350)
        self.assertEqual(p.value('last'), 2.5)

    def test_power_precedence_constants_and_runtime(self):
        p = self.iteration_oracle('''print(-2**2, (-2)**2, 2**3**2, 0**0, 0**3, 1**65535)
def constant():
    return pow(2, 8)
constant_value = constant()
pow(2, 8)
base: u16 = 3
exponent: u8 = 5
value = base ** exponent
base **= 2
negative: i16 = -3
odd = negative ** exponent
print(value, base, odd, pow(negative, 2), pow(2, 8))
''')
        self.assertEqual(p.value('value'), 243)
        self.assertEqual(p.value('base'), 9)
        self.assertEqual(p.value('odd'), -243)
        self.assertEqual(p.value('constant_value'), 256)

    def test_power_wraparound_widths_and_nested_evaluation(self):
        p = self.compile('''def base() -> u16:
    print("base")
    return 3
def exponent() -> u8:
    print("exponent")
    return 2
value = base() ** (base() ** exponent())
small: u8 = 20
wrapped = small ** 2
wide: u16 = small ** 2
minus: i8 = -2
negative = minus ** 7
print(value, wrapped, wide, negative)
''')
        p.run()
        self.assertEqual(p.output, b'BASE\rBASE\rEXPONENT\r19683 144 400 -128\r')
        self.assertEqual(p.symbols['wrapped'][2], 'u8')
        self.assertEqual(p.symbols['wide'][2], 'u16')
        for base, exponent in [(0, 65535), (1, 65535), (65535, 65535), (17, 4097)]:
            p = self.compile(f'base: u16 = {base}\nexponent: u16 = {exponent}\nvalue = base ** exponent\n')
            p.run()
            self.assertEqual(p.value('value'), pow(base, exponent, 65536))

    def test_power_and_math_compile_diagnostics(self):
        self.reject('value = 2**16\n', '16-bit integer range')
        self.reject('value = 300**300\n', '16-bit integer range')
        self.reject('value: u8 = pow(2, 8)\n', 'does not fit')
        self.reject('pow(2, 3, 5)\n', 'two arguments')
        self.reject('pow("x", 2)\n', 'binary operator requires')
        self.reject('value = 2**not True\n', 'parentheses')
        self.reject('import math\nmath.sqrt("x")\n', 'cannot assign')
        self.reject('import math\nmath.log(2, 10)\n', 'expects 1 arguments')
        self.reject('import math\nmath.hypot(3, 4)\n', 'unsupported or unknown')
        self.reject('import math\nmath.pi = 3.0\n', 'constant')
        self.reject('from pyc64._math import nope\n', 'unknown pyc64._math export')

    @unittest.skipUnless(MATH_ROMS, 'configure C64 BASIC and KERNAL ROMs for math execution checks')
    def test_float_power_python_values_signed_exponents_and_restart(self):
        source = '''def base() -> float:
    print("base")
    return 2.5
def exponent() -> float:
    print("exponent")
    return 3.0
result = base() ** exponent()
signed: i16 = -2
reciprocal = 2 ** signed
positive: i8 = 3
promoted = 2 ** positive
zero = 0.0 ** 0
negative = (-2.0) ** 3
fractional = pow(9.0, 0.5)
constant_reciprocal = 2 ** -2
augmented = 3.0
augmented **= 2
right = 2.0 ** 3 ** 2
unary = -2.0 ** 2
print(result, reciprocal, negative, fractional, augmented)
'''
        p = self.float_compile(source)
        before = p.cpu.memory[2:0x90]
        for _ in range(2):
            p.output.clear()
            p.clobber_kernal = True
            p.run()
            self.assertEqual(p.output, b'BASE\rEXPONENT\r15.625 0.25 -8.0 3.0 9.0\r')
            for name, wanted in [('result', 15.625), ('reciprocal', 0.25), ('zero', 1),
                                 ('negative', -8), ('fractional', 3), ('promoted', 8),
                                 ('constant_reciprocal', 0.25), ('augmented', 9),
                                 ('right', 512), ('unary', -4)]:
                self.assertTrue(math.isclose(p.value(name), wanted, rel_tol=1e-8), (name, p.value(name)))
            self.assertEqual(p.symbols['promoted'][2], 'float')
            self.assertEqual(p.cpu.memory[2:0x90], before)
            self.assertEqual(p.cpu.memory[0x300:0x302], [0x3A, 0xA4])
            self.assertEqual(p.cpu.sp, 0xF0)
            p.restart()

    @unittest.skipUnless(MATH_ROMS, 'configure C64 BASIC and KERNAL ROMs for math execution checks')
    def test_standard_math_functions_against_python(self):
        p = self.float_compile('''import math as m
from math import sqrt as root, pi
from math import pow
math_power = pow(2, 3)
a = root(2)
b = m.exp(1)
c = m.log(2)
d = m.sin(pi / 6)
e = m.cos(pi / 3)
f = m.tan(pi / 4)
g = m.atan(1)
h = m.fabs(-2.5)
i = m.pow(-3, 3)
j = m.log2(8)
k = m.log10(100)
l = m.radians(180)
n = m.degrees(pi)
constants = m.e + m.tau
nested = m.sqrt(m.pow(3, 2) + m.pow(4, 2))
print(nested, h, i)
''')
        p.run()
        for name, wanted in [('a', math.sqrt(2)), ('b', math.e), ('c', math.log(2)),
                             ('d', 0.5), ('e', 0.5), ('f', 1), ('g', math.pi / 4),
                             ('h', 2.5), ('i', -27), ('j', 3), ('k', 2),
                             ('l', math.pi), ('n', 180), ('constants', math.e + math.tau),
                             ('nested', 5)]:
            self.assertTrue(math.isclose(p.value(name), wanted, rel_tol=2e-8), (name, p.value(name), wanted))
        self.assertEqual(p.output, b'5.0 2.5 -27.0\r')
        self.assertEqual(p.symbols['math_power'][2], 'float')
        self.assertAlmostEqual(p.value('math_power'), 8)

    @unittest.skipUnless(MATH_ROMS, 'configure C64 BASIC and KERNAL ROMs for math execution checks')
    def test_math_domain_zero_power_and_overflow_guards(self):
        for expression in ['0.0**-1', 'pow(0, -2)', '(-2.0)**0.5',
                           'math.pow(0, -2)', 'math.sqrt(-1)', 'math.log(0)',
                           'math.exp(100)', '1e20**2']:
            with self.subTest(expression=expression):
                p = self.float_compile('import math\nresult = ' + expression + '\nprint("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertNotIn(b'UNREACHABLE', p.output)

    @unittest.skipUnless(MATH_ROMS, 'configure C64 BASIC and KERNAL ROMs for math execution checks')
    def test_circle_area_with_numeric_input(self):
        p = self.float_compile('''import math
print("Let's find the area of a circle!")
r = float(input("What is the radius of the circle? "))
area = (r**2)*math.pi
print("The area of the circle is: ")
print(area)
''')
        for _ in range(2):
            p.output.clear()
            p.keyboard = list(b'2\r')
            p.run()
            self.assertTrue(math.isclose(p.value('area'), 4 * math.pi, rel_tol=1e-8))
            self.assertIn(b'12.5663706\r', p.output)
            p.restart()

    def test_math_import_pruning_reexports_and_user_shadowing(self):
        result, binary, mapping = self.invoke('import math\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        imported_size = binary.stat().st_size
        result, plain, _ = self.invoke('print("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        # Importing unused constants/functions allocates no floating runtime.
        self.assertEqual(imported_size, plain.stat().st_size)
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as directory:
            directory = Path(directory)
            (directory / 'math.py').write_text('answer = 42\n')
            (directory / 'main.py').write_text('import math\nprint(math.answer)\n')
            result = subprocess.run([str(COMPILER), 'check', str(directory / 'main.py')], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            (directory / 'math.py').unlink()
            (directory / 'bridge.py').write_text('from math import sqrt as root, pi\n')
            (directory / 'main.py').write_text('from bridge import root, pi\nprint(root(pi))\n')
            result = subprocess.run([str(COMPILER), 'check', str(directory / 'main.py')], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_math_source_library_host_python_compatibility(self):
        result = subprocess.run([sys.executable, '-c',
            'import math; from pyc64 import _math_library as m; '
            'assert math.__name__ == "math"; assert m.pi == math.pi; '
            'assert m.sqrt(9) == 3; assert m.pow(2, -2) == .25; '
            'assert m.radians(180) == math.pi; assert m.degrees(math.pi) == 180; '
            'assert m.log2(8) == 3; assert m.log10(100) == 2'],
            cwd=ROOT / 'stdlib', text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_integer_conversion_literals_constants_and_annotations(self):
        p = self.iteration_oracle('''from typing import Final
ZERO: Final[int] = int()
LIMIT: Final[u16] = u16(65535.9)
NEGATIVE: Final[i16] = int(-12.75)
class Reading:
    value: int = int(-1.9)
reading = Reading()
positive: int = int(12.9)
negative = int(-12.9)
unchanged = int(65535)
flag = int(True)
print(ZERO, LIMIT, NEGATIVE, reading.value, positive, negative, unchanged, flag)
print(u8(257.9), i8(255.9), u16(-1.9), i16(65535.9), int(-32768.5))
''')
        self.assertEqual(p.output, b'0 65535 -12 -1 12 -12 65535 1\r1 -1 65535 -1 -32768\r')
        self.assertEqual(p.symbols['positive'][2], 'i16')
        self.assertEqual(p.symbols['unchanged'][2], 'u16')
        for value in ['32768.0', '-32769.0', '1e30']:
            self.reject('result = int(' + value + ')\n', 'outside the i16 range')

    def test_fixed_width_casts_from_runtime_float_bits(self):
        rng = random.Random(6010)
        values = [0, 0.999, -0.999, 127.9, -128.9, 255.9, 256.9, 32767.9,
                  -32768.9, 65535.9, 65536.9, 16777215.5, 2147483647.5,
                  4294967295.0, 4294967297.0, 140737488355328.0, 1e38, -1e38, 1e-39]
        values += [rng.uniform(-1.99, 1.99) * 2.0 ** exponent
                   for exponent in [-110, -1, 0, 7, 8, 14, 15, 16, 23, 30, 31, 32, 40, 47, 80, 120]]
        for value in values:
            with self.subTest(value=value):
                p = self.compile(f'value = {value!r}\nbyte = u8(value)\nsigned_byte = i8(value)\n'
                                 'word = u16(value)\nsigned_word = i16(value)\n'
                                 f'FOLDED: Final[u16] = u16({value!r})\nfolded = FOLDED\n')
                # No ROM is loaded: conversion reads the packed target bits.
                p.run()
                truncated = int(p.value('value'))
                word, byte = truncated & 65535, truncated & 255
                self.assertEqual(p.value('word'), word)
                self.assertEqual(p.value('byte'), byte)
                self.assertEqual(p.value('signed_word'), word - 65536 if word >= 32768 else word)
                self.assertEqual(p.value('signed_byte'), byte - 256 if byte >= 128 else byte)
                self.assertEqual(p.value('folded'), word)

    def test_integer_conversion_nested_functions_properties_and_guards(self):
        p = self.iteration_oracle('''class Reading:
    _value: float = -12.75
    @property
    def value(self) -> float:
        print("get")
        return self._value
def real() -> float:
    print("real")
    return 100.9
def combine(first: int, second: int) -> int:
    return first + second
reading = Reading()
value = combine(int(reading.value), int(real()))
reading._value = 32767.75
high = int(reading.value)
reading._value = -32768.75
low = int(reading.value)
small = -0.9
zero = int(small)
print(value, high, low, zero)
''')
        self.assertEqual(p.value('value'), 88)
        p = self.compile('''def byte() -> u8:
    return u8(255.9) + 1
def signed() -> i16:
    return int(32767.9) + 1
byte_value = byte()
signed_value = signed()
''')
        p.run()
        self.assertEqual(p.value('byte_value'), 0)
        self.assertEqual(p.value('signed_value'), -32768)
        for value in [32768.0, -32769.0, 1e30, -1e30]:
            p = self.compile(f'value = {value!r}\nresult = int(value)\nprint("unreachable")\n')
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()
            self.assertNotIn(b'UNREACHABLE', p.output)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for integer string execution checks')
    def test_integer_decimal_strings_and_parser_mode_reset(self):
        for value in ['0', '-0', '+12', ' -32768 ', '32_767', '\t-12\n', '00042']:
            with self.subTest(value=value):
                p = self.float_compile(f'text = {value!r}\nresult = int(text)\n'
                                       'decimal = float("1.25e2")\nagain = int("7")\n')
                p.run()
                self.assertEqual(p.value('result'), int(value))
                self.assertEqual(p.value('decimal'), 125)
                self.assertEqual(p.value('again'), 7)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for integer string execution checks')
    def test_integer_input_reentry_and_invalid_decimal_strings(self):
        p = self.float_compile('''count = int(input("Count? "))
total: int = 0
for index in range(count):
    total += index
print("Total:", total)
''')
        before = p.cpu.memory[2:0x90]
        for _ in range(2):
            p.output.clear()
            p.keyboard = list(b'5\r')
            p.clobber_kernal = True
            p.run()
            self.assertEqual(p.output, b'COUNT? 5\rTOTAL: 10\r')
            self.assertEqual(p.cpu.memory[2:0x90], before)
            self.assertEqual(p.cpu.memory[0x300:0x302], [0x3A, 0xA4])
            p.restart()
        for text in ['', ' ', '.', '12.0', '1e2', '0x10', '1_ ', '1__2', '+ 1',
                     '--1', '1 2', '32768', '-32769', '9999999999999999999999999999999999999999']:
            with self.subTest(text=text):
                p = self.float_compile(f'result = int({text!r})\nprint("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertNotIn(b'UNREACHABLE', p.output)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for rounding execution checks')
    def test_math_integer_rounding_against_python_and_boundaries(self):
        for value in [0.0, 0.1, -0.1, 1.0, -1.0, 1.9, -1.9, 255.99, -255.99,
                      32766.9, -32767.9, 1e-39]:
            p = self.float_compile(f'import math\nvalue = {value!r}\nfloor = math.floor(value)\n'
                                   'ceil = math.ceil(value)\ntrunc = math.trunc(value)\n')
            p.run()
            rounded = p.value('value')
            for name in ['floor', 'ceil', 'trunc']:
                self.assertEqual(p.value(name), getattr(math, name)(rounded))
                self.assertEqual(p.symbols[name][2], 'i16')
        for expression in ['math.floor(-32768.1)', 'math.ceil(32767.1)',
                           'math.trunc(32768)', 'math.floor(1e30)', 'math.ceil(-1e30)']:
            p = self.float_compile('import math\nresult = ' + expression + '\n')
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()

    def test_integer_conversion_diagnostics_and_host_rounding_exports(self):
        self.reject('int("10", 2)\n', 'bases are unsupported')
        self.reject('int([1])\n', 'int requires')
        self.reject('value: int = 40000\n', 'does not fit')
        self.reject('from math import floor\nfloor("x")\n', 'cannot assign')
        result = subprocess.run([sys.executable, '-c',
            'from pyc64._math_library import floor, ceil, trunc; '
            'assert [floor(-1.5), ceil(-1.5), trunc(-1.5)] == [-2, -1, -1]'],
            cwd=ROOT / 'stdlib', text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_property_scalar_accessors_and_python_order(self):
        p = self.iteration_oracle('''class Meter:
    _value: u16 = 10
    _label: str = "start"

    @property
    def value(self):
        print("get", self._value)
        return self._value

    @value.setter
    def value(self, value):
        print("set", value)
        self._value = value

    @property
    def label(self) -> str:
        return self._label

    @label.setter
    def label(self, value: str):
        self._label = value

    @property
    def active(self) -> bool:
        return self._value > 0

def rhs(value):
    print("rhs", value)
    return value

first = Meter()
second = Meter()
first.value = rhs(300)
first.value += rhs(5)
first.label = "updated"
print(first.value, second.value, first.label, first.active)
''')
        self.assertEqual(p.value('first._value'), 305)
        self.assertEqual(p.value('second._value'), 10)
        self.assertEqual(p.symbols['Meter.value.set.value'][2], 'u16')
        self.assertEqual(p.symbols['first'][1], 4)

    def test_property_reference_arguments_and_nested_snapshots(self):
        p = self.iteration_oracle('''class P:
    _value: u16 = 1
    @property
    def value(self):
        return self._value
    @value.setter
    def value(self, value):
        self._value = value

def change(p, value):
    p.value = value
    return p.value

def combine(first, second):
    return first + second

p = P()
q = P()
result = combine(p.value, change(p, 500))
print(result, p.value, q.value)
''')
        self.assertEqual(p.value('result'), 501)
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.value('result'), 501)

    def test_property_import_aliases_and_unused_accessors(self):
        self.module('objects.py', '''from pyc64.types import u16
class P:
    _value: u16 = 2
    @property
    def value(self):
        return self._value
    @value.setter
    def value(self, value):
        self._value = value
    @property
    def unused(self):
        return unknown()
    @unused.setter
    def unused(self, value):
        unknown(value)
''')
        self.module('exports.py', 'from objects import P as Meter\n')
        result, binary, mapping = self.invoke('from exports import Meter\np = Meter()\n'
                                               'p.value = 600\nprint(p.value)\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('function objects.P.value.get ', mapping.read_text())
        self.assertIn('function objects.P.value.set ', mapping.read_text())
        self.assertNotIn('function objects.P.unused.', mapping.read_text())
        p = self.compile('import objects as obj\np = obj.P()\np.value = 600\nprint(p.value)\n')
        p.run()
        self.assertEqual(p.output, b'600\r')

    def test_property_diagnostics_and_initialization(self):
        cases = [
            ('@property\ndef value():\n    return 1\n', 'require a class method'),
            ('class P:\n    @staticmethod\n    def value(self):\n        return 1\n', 'only @property'),
            ('class P:\n    @property\n    def value(self, extra):\n        return 1\n', 'getter requires'),
            ('class P:\n    @value.setter\n    def value(self, value):\n        pass\n', 'preceding @property'),
            ('class P:\n    @property\n    def value(self):\n        return 1\n'
             '    @value.setter\n    def other(self, value):\n        pass\n', 'same name'),
            ('class P:\n    @property\n    def value(self):\n        return 1\n'
             '    @value.setter\n    def value(self, value) -> u8:\n        return value\n', 'return None'),
            ('class P:\n    @property\n    def value(self):\n        return 1\n'
             'p = P()\np.value = 2\n', 'read-only'),
            ('class P:\n    @property\n    def value(self):\n        pass\n'
             'p = P()\nprint(p.value)\n', 'getter must return'),
            ('class P:\n    value: u8 = 1\n    @property\n    def value(self):\n        return 1\n', 'conflicts with field'),
            ('class P:\n    @property\n    def value(self):\n        return 1\n'
             '    def value(self):\n        return 2\n', 'conflicts with property'),
            ('class P:\n    _value: u8\n    @property\n    def value(self):\n        return self._value\n'
             '    def __init__(self):\n        print(self.value)\n        self._value = 1\np = P()\n', 'before initialization'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.reject(source, error)

    def test_property_recursion_and_stack_guards(self):
        self.reject('class P:\n    @property\n    def value(self) -> u8:\n'
                    '        return self.value\np = P()\nprint(p.value)\n', 'recursion')
        self.reject('class P:\n    @property\n    def value(self) -> u8:\n        return 1\n'
                    '    @value.setter\n    def value(self, value: u8):\n'
                    '        self.value = value\np = P()\np.value = 1\n', 'recursion')
        definitions = 'def f0() -> u8:\n    return 1\n'
        definitions += ''.join(f'def f{i}() -> u8:\n    return f{i-1}()\n' for i in range(1, 64))
        self.reject(definitions + 'class P:\n    @property\n    def value(self) -> u8:\n'
                    '        return f63()\np = P()\nprint(p.value)\n', 'stack')

    def test_classes_example_native_layout_and_restart(self):
        p = self.compile((ROOT / 'examples/classes/main.py').read_text())
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'PLAYER 13 24 80 TRUE\rPLAYER 80 90 0 FALSE\r')
        self.assertEqual([p.value('player.' + name) for name in ('x', 'y', 'health')], [13, 24, 80])
        address, size, kind = p.symbols['player']
        self.assertEqual((size, kind), (5, 'object'))
        self.assertEqual([p.symbols['player.' + name][0] - address
                          for name in ('x', 'y', 'health', 'name')], [0, 1, 2, 3])
        self.assertEqual(p.symbols['Player.damage.self'][1:], (2, 'object'))
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.output, b'PLAYER 13 24 80 TRUE\rPLAYER 80 90 0 FALSE\r')

    def test_class_scalar_fields_defaults_and_python_oracle(self):
        p = self.iteration_oracle('''from typing import Final
BASE: Final[u16] = 500
class Record:
    byte: u8 = 5
    delta: i8 = -20
    word: u16 = BASE + 3
    offset: i16 = -300
    ready: bool = True
    text: str = "Mixed"

    def adjust(self, amount: i8) -> i16:
        self.delta = i8(self.delta + amount)
        self.offset -= 10
        self.text = "Updated"
        self.ready = False
        return self.offset

record = Record()
other: Record = Record()
result = record.adjust(2)
print(record.byte, record.delta, record.word, record.offset, record.ready, record.text, result)
print(other.delta, other.offset, other.ready, other.text)
''')
        address, size, _ = p.symbols['record']
        self.assertEqual(size, 9)
        self.assertEqual([p.symbols['record.' + name][0] - address
                          for name in ('byte', 'delta', 'word', 'offset', 'ready', 'text')],
                         [0, 1, 2, 4, 6, 7])

    def test_class_reference_inference_forwarding_and_same_object_arguments(self):
        self.iteration_oracle('''class Counter:
    value: u16 = 0
    def add(self, amount: u16):
        self.value += amount
    def total(self) -> u16:
        return self.value

def outer(counter, amount):
    inner(counter, amount)
    return counter.total()

def inner(counter, amount):
    counter.add(amount)

def twice(first: Counter, second: Counter):
    first.add(1)
    second.add(2)

counter = Counter()
other = Counter()
print(outer(counter, 500), outer(other, 10))
twice(counter, counter)
Counter.add(other, 20)
print(counter.value, other.value)
''')

    def test_class_constructor_evaluation_order_and_local_instances(self):
        self.iteration_oracle('''events = [0]
class Pair:
    first: u16
    second: u16
    label: str = "Pair"
    def __init__(self, first: u16, second: u16):
        events[0] = events[0] + 1
        print("init", first, second)
        self.first = first
        self.second = second

def argument(value: u16) -> u16:
    events[0] = events[0] + 1
    print("arg", value)
    return value

def make(value: u16) -> u16:
    local = Pair(argument(value), argument(u16(value + 1)))
    print(local.first, local.second, local.label)
    local.label = "Changed"
    return local.second

print(make(10), make(500), events[0])
''')

    def test_class_manual_initialization_and_constructor_paths(self):
        self.iteration_oracle('''class Point:
    x: u8
    y: u8
    def sum(self) -> u16:
        return u16(self.x) + u16(self.y)

class Conditional:
    value: u8
    def __init__(self, choose: bool):
        if choose:
            self.value = 10
        else:
            self.value = 20
        return

point = Point()
point.x = 200
point.y = 100
left = Conditional(True)
right = Conditional(False)
print(point.sum(), left.value, right.value)
''')

    def test_class_modules_annotations_aliases_and_method_names(self):
        self.module('first.py', '''class Item:
    value: u16
    def __init__(self, value: u16):
        self.value = value
    def get(self) -> u16:
        return self.value
owned = Item(500)
''')
        self.module('second.py', '''class Item:
    value: str = "other"
    def get(self) -> str:
        return self.value
''')
        self.module('exports.py', 'from first import Item as Number, owned\n')
        p = self.compile('''import first
import second as strings
from exports import Number, owned as shared
def edit(value: Number):
    value.value += 1
value: Number = Number(1000)
text = strings.Item()
edit(value)
edit(shared)
print(value.get(), text.get(), first.owned.value, shared.get())
''')
        p.run()
        self.assertEqual(p.output, b'1001 OTHER 501 501\r')
        self.assertEqual(p.value('first.owned.value'), 501)

    def test_class_unused_generic_methods_are_pruned(self):
        source = '''class Item:
    value: u8 = 3
    def used(self) -> u8:
        return self.value
    def unused(self, unknown):
        return unknown
item = Item()
print(item.used())
'''
        result, output, mapping = self.invoke(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('Item.unused', mapping.read_text())
        self.assertIn('Item.used', mapping.read_text())

    def test_class_reference_fields_across_pages_and_nested_argument_snapshots(self):
        fields = '    tag: u8 = 1\n' + ''.join(f'    f{i}: u16 = 65534\n' for i in range(31))
        source = ('class Block:\n' + fields
                  + '    def bump(self) -> u16:\n'
                    '        self.f30 = u16(self.f30 + 1)\n'
                    '        return self.f30\n'
                  + ''.join(f'b{i} = Block()\n' for i in range(6))
                  + ''.join(f'print(b{i}.bump(), b{i}.bump(), b{i}.f29)\n' for i in range(6)))
        p = self.iteration_oracle(source)
        self.assertTrue(any(p.symbols[f'b{i}'][0] // 256 != p.symbols[f'b{i}.f30'][0] // 256
                            for i in range(6)))
        self.iteration_oracle('''class Item:
    value: u16 = 10
    def show(self, first: u16, second: u16) -> u16:
        print(first, second, self.value)
        return self.value

def change(item: Item) -> u16:
    item.value = 500
    return 3

item = Item()
print(item.show(item.value, change(item)))
''')

    def test_class_arena_limits(self):
        self.reject(''.join(f'class C{i}:\n    pass\n' for i in range(33)), 'class arena exhausted')
        self.reject('class Large:\n' + ''.join(f'    f{i}: u8 = 0\n' for i in range(33)), 'at most 32 fields')

    def test_class_initialization_diagnostics(self):
        cases = [
            ('class P:\n    x: u8\np = P()\nprint(p.x)\n', 'before initialization'),
            ('class P:\n    x: u8\n    def get(self):\n        return self.x\np = P()\np.get()\n', 'before initialization'),
            ('class P:\n    x: u8\n    def __init__(self):\n        pass\np = P()\n', 'before initialization'),
            ('class P:\n    x: u8\n    def __init__(self, flag: bool):\n        if flag:\n            self.x = 1\np = P(True)\n', 'before initialization'),
            ('class P:\n    x: u8\n    def __init__(self):\n        self.x += 1\np = P()\n', 'before initialization'),
            ('class P:\n    x: u8\n    def __init__(self):\n        return\n        self.x = 1\np = P()\n', 'before initialization'),
            ('class P:\n    x: u8\np = P()\nif True:\n    p.x = 1\nprint(p.x)\n', 'before initialization'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.reject(source, error)

    def test_class_restriction_and_nominal_type_diagnostics(self):
        cases = [
            ('class P:\n    x = 1\n', 'annotated scalar fields'),
            ('class P:\n    x: list[u8] = [1]\n', 'annotated scalar fields'),
            ('class P:\n    x: u8 = peek(4096)\n', 'compile-time scalar'),
            ('class P:\n    x: u8 = 300\n', 'does not fit'),
            ('class P:\n    x: u8\n    x: u8\n', 'duplicate field'),
            ('class P:\n    x: u8\n    def x(self):\n        pass\n', 'conflicts with field'),
            ('class P:\n    def method():\n        pass\n', 'self parameter'),
            ('class P:\n    def method(value):\n        pass\n', 'first method parameter'),
            ('class P:\n    def __init__(self) -> u8:\n        return 1\n', '__init__ must return None'),
            ('class P:\n    pass\np = P(1)\n', 'expects no arguments'),
            ('class P:\n    pass\np = P()\nq = p\n', 'object aliases'),
            ('class P:\n    pass\np = P()\np = P()\n', 'object rebinding'),
            ('class P:\n    pass\np = P()\nvalues = [p]\n', 'list elements'),
            ('class P:\n    pass\np = P()\nprint(p)\n', 'print supports'),
            ('class P:\n    pass\np = P()\nprint(p.missing)\n', 'has no field'),
            ('class P:\n    pass\ndef f(value: P):\n    return value\np = P()\nf(p)\n', 'object return values'),
            ('class P:\n    pass\nclass Q:\n    pass\ndef f(value: P):\n    pass\nq = Q()\nf(q)\n', 'same class type'),
            ('class P:\n    pass\nclass Q(P):\n    pass\n', 'inheritance'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.reject(source, error)

    def test_class_recursion_and_expression_stack_guards(self):
        self.reject('class P:\n    def run(self):\n        self.run()\np = P()\np.run()\n', 'recursion')
        self.reject('class P:\n    def first(self):\n        self.second()\n'
                    '    def second(self):\n        self.first()\np = P()\np.first()\n', 'recursion')
        definitions = 'def f0() -> u8:\n    return 1\n'
        definitions += ''.join(f'def f{i}() -> u8:\n    return f{i-1}()\n' for i in range(1, 64))
        self.reject(definitions + 'class P:\n    x: u8\n'
                    '    def __init__(self, x: u8):\n        self.x = x\np = P(f63())\n', 'stack')

    def test_assert_truth_and_lazy_message_python_oracle(self):
        for value in ('True', '500', '-1', '"yes"'):
            with self.subTest(value=value):
                p = self.iteration_oracle('''calls = [0]

def condition(value):
    calls[0] = calls[0] + 1
    return value

def message() -> str:
    calls[0] = calls[0] + 100
    return "unused"

assert condition(''' + value + '''), message()
assert True or condition(''' + value + ''')
print(calls[0])
''')
                self.assertEqual(p.output, b'1\r')

    def test_assert_failure_message_and_stopped_execution(self):
        for condition in ('False', '0', '""', 'value == 4'):
            with self.subTest(condition=condition):
                p = self.compile('''value = 3
calls = [0]
def message():
    calls[0] = calls[0] + 1
    return "assert failed"
assert ''' + condition + ''', message()
value = 99
''')
                p.clobber_kernal = True
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertEqual(p.value('value'), 3)
                self.assertEqual(p.output, b'ASSERT FAILED\r')
                address = p.symbols['calls'][0]
                self.assertEqual(p.cpu.memory[address], 1)
        p = self.compile('assert False\nprint("unreachable")\n')
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        self.assertEqual(p.output, b'')

    def test_assert_diagnostics_recursion_and_stack_budget(self):
        self.reject('values = [1]\nassert values\n', 'assert condition')
        self.reject('assert True, 5\n', 'assert message must be a string')
        self.reject('def f() -> bool:\n    assert f()\n    return True\nf()\n', 'recursion')
        definitions = 'def f0() -> str:\n    return "message"\n'
        definitions += ''.join(f'def f{i}() -> str:\n    return f{i-1}()\n' for i in range(1, 66))
        self.reject(definitions + 'assert False, f65()\n', 'stack')

    def test_random_generation_reseed_and_native_restart(self):
        source = '''from pyc64.random import seed, next_u16
for initial in [0, 1, 42, 65535]:
    seed(initial)
    for index in range(12):
        print(next_u16())
seed(42)
first = next_u16()
seed(42)
again = next_u16()
print(first == again)
'''
        p = self.iteration_oracle(source)
        output = bytes(p.output)
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.output, output)
        # The default module seed is reset on every native program run.
        p = self.compile('from pyc64.random import next_u16\nprint(next_u16())\n')
        p.run()
        output = bytes(p.output)
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.output, output)

    def test_random_period_and_rejection_distribution_under_cpython(self):
        script = '''from pyc64 import random
random.seed(0)
seen = set()
for index in range(65536):
    value = random.next_u16()
    assert 0 <= value <= 65535
    seen.add(value)
assert len(seen) == 65536
assert random._state[0] == 0
for bound in [1, 3, 6, 255, 256, 1000, 32769, 65535]:
    threshold = (65536 - bound) % bound
    counts = [0] * bound
    for value in seen:
        if value >= threshold:
            counts[value % bound] += 1
    assert min(counts) == max(counts)
'''
        result = subprocess.run([sys.executable, '-c', script], cwd=ROOT / 'stdlib',
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_random_bounded_ranges_and_rejection_python_oracle(self):
        self.iteration_oracle('''from pyc64.random import seed, randbelow, randrange, randint, next_u16
seed(42)
for bound in [1, 3, 6, 255, 256, 1000, 32769, 65535]:
    for index in range(6):
        print(randbelow(bound))
print(next_u16())
for index in range(6):
    print(randrange(-32768, 32767), randrange(-10, 11), randrange(5, 6))
    print(randint(-32768, 32767), randint(-1, 0), randint(32767, 32767))
''')

    def test_random_choice_inferred_sequences_and_import_aliases(self):
        cases = [('words = ["Yes", "Absolutely", "Indeed", "Sure"]', 'words'),
                 ('numbers = [i16(-200), i16(500), i16(1000)]', 'numbers'),
                 ('flags = [True, False]', 'flags'),
                 ('values = array(u16, 3)\nvalues[0] = 5\nvalues[1] = 300\nvalues[2] = 65535', 'values'),
                 ('text = "hello"', 'text')]
        for declaration, name in cases:
            with self.subTest(declaration=declaration):
                self.iteration_oracle('import pyc64.random as rng\n'
                                      'from pyc64.random import choice as choose\n'
                                      'rng.seed(42)\n' + declaration + '\n'
                                      'for index in range(12):\n'
                                      f'    print(choose({name}))\n')
        # Choosing does not copy or replace the source container.
        self.iteration_oracle('from pyc64.random import seed, choice\n'
                              'seed(42)\nwords = ["only"]\n'
                              'print(choice(words))\nwords[0] = "changed"\n'
                              'print(choice(words), words[0])\n')

    def test_random_preconditions_stop_before_state_change(self):
        cases = [('randbelow(0)', 'randbelow requires a positive bound'),
                 ('randrange(5, 5)', 'randrange requires start < stop'),
                 ('randrange(6, 5)', 'randrange requires start < stop'),
                 ('randint(6, 5)', 'randint requires low <= high'),
                 ('choice(words)', 'choice requires a nonempty sequence'),
                 ('choice("")', 'choice requires a nonempty sequence')]
        for call, message in cases:
            with self.subTest(call=call):
                p = self.compile('from pyc64.random import seed, randbelow, randrange, randint, choice\n'
                                 'seed(42)\nwords: list[str] = []\n'
                                 + call + '\nprint("unreachable")\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertEqual(p.output, message.upper().encode() + b'\r')
                address = p.symbols['pyc64.random._state'][0]
                self.assertEqual(p.cpu.memory[address:address+2], [42, 0])
                script = library_bindings('from pyc64.random import seed, randbelow, randrange, randint, choice\n'
                                          'seed(42)\nwords: list[str] = []\n' + call + '\n')
                result = subprocess.run([sys.executable, '-c', script], cwd=ROOT / 'stdlib',
                                        text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('AssertionError: ' + message, result.stderr)

    def test_random_example(self):
        p = self.compile((ROOT / 'examples/random/main.py').read_text())
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'8 BALL: YES\rDICE: 1 5\rOFFSET: 10\r'
                                  b'SAMPLE: 39226\rAGAIN: YES\r')
        self.assertEqual(p.value('sample'), 39226)
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])

    def test_string_runtime_comparisons_python_oracle(self):
        # Different literals have different addresses; equality compares contents.
        pairs = [('', ''), ('', 'a'), ('a', ''), ('a', 'a'), ('a', 'A'), ('Z', 'a'),
                 ('abc', 'abcd'), ('abcd', 'abc'), ('abc', 'abd'), ('abz', 'aba'),
                 ('\n', '\r'), ('a' * 260, 'a' * 259 + 'b'), ('a' * 260, 'a' * 260)]
        for left, right in pairs:
            with self.subTest(left=left[-10:], right=right[-10:]):
                source = f'left = {left!r}\nright = {right!r}\n'
                source += 'print(left == right, left != right, left < right, left <= right, left > right, left >= right)\n'
                self.iteration_oracle(source)

    def test_string_runtime_lengths_indices_and_lifetime(self):
        source = '''def text():
    return "Abc"
word = "Abc"
'''
        source += '''index: i16 = -1
first = text()[0]
last = word[index]
word = "new"
print(first, last, len(word), len(text()), ord(first), ord(last))
for character in "xyz":
    print(character)
print(first, last)
print("ABC"[-3], "ABC"[-1], "ABC"[True], len("ABC"[0]))
'''
        self.iteration_oracle(source)
        long_text = 'a' * 260 + 'Z'
        for index in [-261, -260, -1, 0, 255, 256, 260]:
            with self.subTest(index=index):
                p = self.iteration_oracle(f'text = {long_text!r}\nindex: i16 = {index}\n'
                                          'print(len(text), text[index], ord(text[index]))\n')
                self.assertEqual(p.output, f'261 {long_text[index].upper()} {ord(long_text[index])}\r'.encode())

    def test_string_source_and_index_evaluation_order(self):
        self.iteration_oracle('''word = "Abc"
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
''')

    def test_string_truth_and_boolean_operand_selection(self):
        self.iteration_oracle('''empty = ""
word = "Yes"
def effect():
    print("effect")
    return "Later"
print(bool(empty), bool(word), not empty, not word)
print(empty and effect(), word or effect(), word and effect(), empty or effect())
if empty:
    print("unreachable")
else:
    print("empty")
if word:
    print("nonempty")
while word:
    print(word)
    word = ""
else:
    print("exhausted")
print("" and 9, "yes" or 9, "yes" and 9, "" or 9)
''')

    def test_string_constants_and_ascii_characters(self):
        p = self.iteration_oracle(r'''YES: Final = "a" == "a"
CASE: Final = "a" == "A"
ORDER: Final = "Z" < "a"
NEWLINES: Final = "\n" != "\r"
EMPTY: Final = not ""
NONEMPTY: Final = bool("a")
LETTER: Final = ord("a")
FIRST: Final = "Abc"[0]
CHAR: Final = chr(98)
print(YES, CASE, ORDER, NEWLINES, EMPTY, NONEMPTY, LETTER, FIRST, CHAR)
value: u8 = 97
character = chr(value)
saved = character
value = 98
other = chr(value)
print(character, saved, other, ord(saved), chr(127) > chr(1))
''')
        self.assertEqual(p.value('character'), p.value('saved'))
        for value in [1, 8, 9, 10, 11, 12, 13, 32, 65, 90, 97, 122, 127]:
            with self.subTest(value=value):
                p = self.compile(f'value: u8 = {value}\ntext = chr(value)\nresult = ord(text)\nlength = len(text)\n')
                p.run()
                self.assertEqual([p.value('result'), p.value('length')], [value, 1])

    def test_string_index_and_character_runtime_guards(self):
        for index, kind, text in [(-4, 'i16', 'abc'), (3, 'u8', 'abc'),
                                  (65535, 'u16', 'abc'), (-32768, 'i16', 'abc'), (0, 'u8', '')]:
            with self.subTest(index=index, text=text):
                p = self.compile(f'text = {text!r}\nindex: {kind} = {index}\nresult = text[index]\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
        for value in [0, 128, 65535, -1]:
            with self.subTest(value=value):
                p = self.compile(f'value: i16 = {value}\nresult = chr(value)\n'
                                 if value <= 32767 else f'value: u16 = {value}\nresult = chr(value)\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
        for text in ['', 'ab']:
            p = self.compile(f'text = {text!r}\nresult = ord(text)\n')
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()

    def test_string_restriction_diagnostics(self):
        cases = [('print("abc"[3])\n', 'string index is out of bounds'),
                 ('print("abc"[-4])\n', 'string index is out of bounds'),
                 ('text = "abc"\ntext[0] = "z"\n', 'strings are immutable'),
                 ('text = "abc"\ntext[0] += "z"\n', 'strings are immutable'),
                 ('print(ord(""))\n', 'one-character string'),
                 ('print(ord("ab"))\n', 'one-character string'),
                 ('print(chr(0))\n', 'ASCII values'), ('print(chr(128))\n', 'ASCII values'),
                 ('print(chr("a"))\n', 'requires an integer'),
                 ('print(ord(3))\n', 'ord requires'),
                 ('text = "a" + "b"\n', 'unsupported string operator'),
                 ('text = "a"\nnumber = 3\nprint(text == number)\n', 'requires integers'),
                 ('print("abc"["a"])\n', 'string index must be an integer')]
        for source, error in cases:
            with self.subTest(source=source):
                self.reject(source, error)

    def test_strings_library_matches_cpython_operations(self):
        sys.path.insert(0, str(ROOT / 'stdlib'))
        try:
            from pyc64 import strings
            for text in ['', 'banana', 'Abc Abc', 'aaaaa', 'a' * 260 + 'b']:
                for pattern in ['', 'a', 'an', 'aa', 'Abc', 'abc', 'b', 'xxxxxx', text]:
                    with self.subTest(length=len(text), pattern=pattern[:15]):
                        self.assertEqual(strings.find(text, pattern), text.find(pattern))
                        self.assertEqual(strings.count(text, pattern), text.count(pattern))
                        self.assertEqual(strings.startswith(text, pattern), text.startswith(pattern))
                        self.assertEqual(strings.endswith(text, pattern), text.endswith(pattern))
                        self.assertEqual(strings.contains(text, pattern), pattern in text)
            for text in ['', '123', '0', '12a', '  ', '\t\n\r\v\f', ' x']:
                self.assertEqual(strings.isdigit(text), text.isdigit())
                self.assertEqual(strings.isspace(text), text.isspace())
            for value in range(128):
                self.assertEqual(strings.isdigit(chr(value)), chr(value).isdigit())
                self.assertEqual(strings.isspace(chr(value)), chr(value).isspace())
        finally:
            sys.path.pop(0)
        for text, pattern in [('', ''), ('', 'x'), ('banana', 'an'), ('aaaaa', 'aa'),
                              ('Abc', 'abc'), ('Abc Abc', 'Abc'), ('abc', 'abcd')]:
            source = ('from pyc64.strings import find, count, startswith, endswith, contains\n'
                      f'text = {text!r}\npattern = {pattern!r}\n'
                      'print(find(text, pattern), count(text, pattern), startswith(text, pattern), '
                      'endswith(text, pattern), contains(text, pattern))\n')
            self.iteration_oracle(source)
        self.iteration_oracle('from pyc64.strings import isdigit, isspace\n'
                              'for text in ["", "123", "12a", " ", "\\t\\n\\r\\v\\f", " x"]:\n'
                              '    print(isdigit(text), isspace(text))\n')
        self.iteration_oracle('from pyc64.strings import isdigit, isspace\n'
                              'for value in range(1, 128):\n'
                              '    text = chr(value)\n'
                              '    print(value, isdigit(text), isspace(text))\n')

    def test_string_helpers_page_crossing_and_restart(self):
        p = self.compile('from pyc64.strings import find, count, startswith, endswith\n'
                         'text = "' + 'a' * 260 + 'b"\n'
                         'position = find(text, "b")\namount = count(text, "aa")\n'
                         'print(position, amount, startswith(text, "aaa"), endswith(text, "ab"))\n')
        p.run(limit=3_000_000)
        self.assertEqual(p.output, b'260 130 TRUE TRUE\r')
        p.restart()
        p.output.clear()
        p.run(limit=3_000_000)
        self.assertEqual(p.output, b'260 130 TRUE TRUE\r')

    def test_strings_example(self):
        p = self.compile((ROOT / 'examples/strings/main.py').read_text())
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'TEXT: HELLO C64! LEN: 10\rFIRST: H LAST: !\rFIND: 6 COUNT: 2\r'
                                  b'MATCH: TRUE TRUE FALSE\rCASE: FALSE TRUE\rDIGITS: TRUE ASCII: 97 A\r')
        self.assertEqual([p.value('length'), p.value('found')], [10, 6])

    def test_string_c64_io_conversion_preserves_ascii_values(self):
        p = self.compile((ROOT / 'tests/fixtures/string_disk.py').read_text())
        p.disk = DiskKernal()
        p.run()
        self.assertEqual(p.disk.files[b'PYC64-CASE'], b'ABC\rX')
        self.assertEqual(p.output, b'TEXT IO: TRUE TRUE TRUE\rASCII: 98 10\r')
        self.assertEqual([p.value('letter'), p.value('newline'), p.value('unchanged')], [98, 10, 1])
        self.assertEqual(p.disk.opened, [(2, 8, 2, b'PYC64-CASE,S,W')])

    def test_string_expression_hardware_stack_budget(self):
        definitions = 'def f0() -> u8:\n    return 0\n'
        definitions += ''.join(f'def f{i}() -> u8:\n    return f{i-1}()\n' for i in range(1, 64))
        # String indexing holds a two-byte source pointer across the index call.
        self.reject(definitions + 'text = "a"\nresult = text[f63()]\n', 'stack')
        definitions = 'def f0() -> str:\n    return "a"\n'
        definitions += ''.join(f'def f{i}() -> str:\n    return f{i-1}()\n' for i in range(1, 65))
        self.reject(definitions + 'result = f64()[0]\n', 'stack')
        p = self.compile(definitions + 'result = f63()[0]\nprint(result)\n')
        p.run()
        self.assertEqual(p.output, b'A\r')

    def iteration_oracle(self, source):
        source = library_bindings(source)
        result = subprocess.run([sys.executable, '-c', source], cwd=ROOT / 'stdlib',
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        p = self.compile(source)
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, result.stdout.upper().replace('\n', '\r').encode())
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
        return p

    def test_direct_iteration_scalar_types_python_oracle(self):
        p = self.iteration_oracle('''numbers = [1, 500, 65535]
signed = [-128, 127, 250]
flags = [False, True]
words = ["Yes", "Indeed"]
for number in numbers:
    print(number)
for negative in signed:
    print(negative)
for flag in flags:
    print(flag)
for word in words:
    print(word)
for temporary in [10, 20, 30]:
    print(temporary)
for zero in array(u16, 3):
    print(zero)
''')
        self.assertEqual(p.symbols['number'][2], 'u16')
        self.assertEqual(p.symbols['negative'][2], 'i16')
        self.assertEqual(p.symbols['flag'][2], 'bool')
        self.assertEqual(p.symbols['word'][2], 'str')

    def test_iteration_mutation_and_independent_progression(self):
        self.iteration_oracle('''values = [1, 2, 3]
position: u8 = 0
item: u16 = 10
for item in values:
    print(item)
    if position == 0:
        values[2] = 7
    item = 99
    position += 1
print(item, values[-1], position)
def change(buffer: list[u8]):
    buffer[1] = 42
for observed in values:
    print(observed)
    change(values)
''')

    def test_iteration_references_nested_calls_and_page_crossing(self):
        p = self.iteration_oracle('''def total(values) -> u16:
    result: u16 = 0
    for value in values:
        result += value
    return result
def forwarded(values):
    return total(values)
values = array(u16, 300)
for index in range(300):
    values[index] = u16(index)
print(forwarded(values))
short = array(u16, 1)
short[0] = 700
print(forwarded(short))
def display(words: list[str]):
    for word in words:
        for character in word:
            print(character)
words = ["ab", "CD"]
display(words)
empty: list[str] = []
display(empty)
''')
        self.assertEqual(p.value('total.result'), 700)
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.output, b'44850\r700\rA\rB\rC\rD\r')

    def test_iteration_literal_evaluation_snapshot(self):
        self.iteration_oracle('''values = [10, 20]
def effect(number):
    print(number)
    values[0] = 99
    return number
for value in [effect(values[0]), effect(values[1]), values[0]]:
    print(value)
''')

    def test_string_iteration_snapshot_and_character_lifetime(self):
        self.iteration_oracle('''text = "abc"
first = ""
last = ""
position: u8 = 0
def echo(character):
    return character
for character in text:
    if position == 0:
        first = echo(character)
        text = "xyz"
    last = character
    position += 1
print(first, last, text, position)
for next_character in "de":
    print(next_character)
print(first, last)
def original():
    print("capture")
    return "pq"
for returned in original():
    print(returned)
''')
        p = self.compile('text = "' + 'a' * 260 + 'z' + '"\n'
                         'count: u16 = 0\nlast = ""\n'
                         'for character in text:\n    count += 1\n    last = character\n'
                         'print(count, last)\n')
        p.run()
        self.assertEqual(p.output, b'261 Z\r')

    def test_empty_iteration_retains_existing_variables(self):
        self.iteration_oracle('''number: u16 = 99
word = "before"
empty = []
for number in empty:
    print("unreachable")
else:
    print("empty list")
for word in "":
    print("unreachable")
else:
    print("empty string")
for fresh in []:
    print(fresh)
for direct_empty in []:
    pass
print(number, word)
''')

    def test_loop_else_break_continue_python_oracle(self):
        self.iteration_oracle('''for value in [1, 2, 3]:
    if value == 2:
        continue
    print(value)
else:
    print("list exhausted")
for word in ["a", "b"]:
    print(word)
    break
else:
    print("unreachable")
for index in range(0,):
    print("unreachable")
else:
    print("empty range")
stop: i16 = 4
for index in range(1, stop, 1,):
    if index == 2:
        continue
    print(index)
else:
    print("runtime range exhausted")
remaining: u8 = 2
while remaining:
    remaining -= 1
    continue
else:
    print("while exhausted")
while True:
    break
else:
    print("unreachable")
while False:
    pass
else:
    print("false condition")
''')

    def test_nested_loop_else_targets_outer_loop(self):
        self.iteration_oracle('''for outer in [1, 2, 3]:
    for inner in [4, 5]:
        print(outer, inner)
        break
    else:
        print("unreachable")
else:
    print("outer exhausted")
for outer in range(3):
    while False:
        pass
    else:
        print("outer break", outer)
        break
else:
    print("unreachable")
for outer in [1, 2]:
    for inner in []:
        pass
    else:
        continue
    print("unreachable")
else:
    print("outer continued")
''')

    def test_loop_else_returns_and_definite_initialization(self):
        self.iteration_oracle('''def find(values):
    for value in values:
        if value > 5:
            return value
    else:
        return 0
def while_result(flag):
    while flag:
        return 7
    else:
        return 9
def nested_result():
    for outer in [1]:
        for inner in [2]:
            break
    else:
        return 10
small = [1, 2]
large = [1, 9]
print(find(small), find(large), while_result(False), while_result(True), nested_result())
for value in []:
    pass
else:
    initialized = 42
print(initialized)
while False:
    pass
else:
    another = 10
print(another)
''')

    def test_iteration_and_loop_else_diagnostics(self):
        cases = [
            ('for item in 7:\n    pass\n', 'iteration requires'),
            ('for item in [1]:\n    pass\nprint(item)\n', 'before initialization'),
            ('values = [1]\nfor item in values:\n    values = [2]\n', 'rebinding an iterated'),
            ('values = array(u8, 2)\nfor item in values:\n    values = array(u8, 2)\n', 'rebinding an iterated'),
            ('values = [1]\nfor item in values:\n    for nested in values:\n        pass\n'
             '    else:\n        values = [2]\n', 'rebinding an iterated'),
            ('item: u8 = 0\nfor item in [300]:\n    pass\n', 'cannot assign u16'),
            ('item = 0\nfor item in ["a"]:\n    pass\n', 'cannot assign str'),
            ('COUNT: Final = 1\nfor COUNT in [1]:\n    pass\n', 'cannot use constant'),
            ('for item in []:\n    pass\nelse:\n    break\n', 'outside a loop'),
            ('while False:\n    pass\nelse:\n    continue\n', 'outside a loop'),
            ('for item in [1]:\n    break\nelse:\n    value = 9\nprint(value)\n', 'before initialization'),
            ('def bad():\n    for value in [1]:\n        while False:\n            pass\n'
             '        else:\n            break\n    else:\n        return 3\nbad()\n', 'must return on every path'),
            ('values = [1]\ndef bad() -> None:\n    for values in values:\n        pass\n', 'before initialization'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.reject(source, error)

    def test_loop_else_call_graph_inference_recursion_and_liveness(self):
        self.module('helpers.py', '''def echo(value):
    return value
def unused(text: str):
    for character in text:
        print(character)
''')
        p = self.compile('from helpers import echo, unused\nfor value in []:\n    pass\n'
                         'else:\n    result = echo(300)\nprint(result)\n')
        p.run()
        self.assertEqual(p.output, b'300\r')
        self.assertEqual(p.symbols['helpers.echo.value'][2], 'u16')
        self.assertFalse(any('unused' in name for name in p.symbols))
        plain = self.compile('print(7)\n')
        unused = self.compile('def unused(text: str):\n    for character in text:\n'
                              '        print(character)\nprint(7)\n')
        self.assertEqual(plain.cpu.memory[0x0801:0xA000], unused.cpu.memory[0x0801:0xA000])
        self.reject('def recursive() -> None:\n    while False:\n        pass\n'
                    '    else:\n        recursive()\nrecursive()\n', 'recursion')
        self.reject('def recursive() -> u8:\n    for item in [recursive()]:\n'
                    '        pass\n    return 1\nrecursive()\n', 'recursion')

    def test_range_overflow_exhaustion_runs_else(self):
        for bounds in [(32765, 32767, 2), (-32767, -32768, -2), (0, -32768, -32768)]:
            for runtime in [False, True]:
                with self.subTest(bounds=bounds, runtime=runtime):
                    source = ('start: i16 = %d\nstop: i16 = %d\nstep: i16 = %d\n' % bounds
                              if runtime else '')
                    arguments = 'start, stop, step' if runtime else ', '.join(map(str, bounds))
                    source += f'for value in range({arguments}):\n    print(value)\nelse:\n    print("exhausted")\n'
                    self.iteration_oracle(source)

    def test_iteration_example(self):
        p = self.compile((ROOT / 'examples/iteration/main.py').read_text())
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'SUM: 60\rYES\rABSOLUTELY\rINDEED\rSURE\rALL ANSWERS\r'
                                  b'FOUND: 300\rC\r6\r4\rSAVED: C\rFINISHED\r')
        self.assertEqual(p.value('total'), 60)

    def test_collections_libraries_and_buffer_example(self):
        p = self.compile((ROOT / 'examples/buffers/main.py').read_text())
        p.run()
        self.assertEqual(p.output, b'COPIED: 3 VALUES: 11 31 8\rWORDS: FIRST CHANGED\r')
        self.assertEqual(p.value('copied'), 3)
        for text, output in [
            ('from pyc64.collections import fill\nvalues = ["a", "b"]\n'
             'fill(values, "done")\nprint(values[0], values[-1])\n', b'DONE DONE\r'),
            ('from pyc64.collections import fill\nvalues = [False, False]\n'
             'fill(values, True)\nprint(values[0], values[1])\n', b'TRUE TRUE\r'),
            ('from pyc64.collections import copy_into\nsource = [1, 2, 3]\n'
             'destination = [10, 20]\ncount = copy_into(source, destination, 9)\n'
             'destination[0] = 99\nprint(count, source[0], destination[0], destination[1])\n', b'2 1 99 2\r')]:
            with self.subTest(text=text):
                p = self.compile(text)
                p.run()
                self.assertEqual(p.output, output)

    def test_unused_generic_library_functions_and_exports(self):
        self.module('helpers.py', '''marker = 7
def generic(values):
    return len(values)
def unused(values):
    return values[0]
''')
        self.module('exports.py', 'from helpers import generic, unused\n')
        p = self.compile('from exports import generic, unused\nprint("main")\n')
        p.run()
        self.assertEqual(p.value('helpers.marker'), 7)
        self.assertFalse(any(name.startswith('helpers.generic.') or name.startswith('helpers.unused.')
                             for name in p.symbols))
        p = self.compile('from exports import generic\nvalues = [1, 2, 3]\n'
                         'result = generic(values)\n')
        p.run()
        self.assertEqual(p.value('result'), 3)
        # Uncalled user functions still need enough type information to check.
        self.reject('def generic(values):\n    return len(values)\n', 'cannot infer parameter')

    def test_disk_buffers_demo_roundtrip_and_existing_file(self):
        p = self.compile((ROOT / 'examples/disk_buffers/main.py').read_text())
        p.disk = DiskKernal()
        p.run()
        self.assertEqual([p.value(n) for n in ('written', 'received', 'write_ok', 'read_ok', 'dos')],
                         [3, 3, 1, 1, 0])
        self.assertEqual(p.output, b'BUFFERS: 3 3 TRUE TRUE\rBYTES: 0 255 42 0\rDOS: 0\r')
        self.assertEqual(p.disk.files[b'PYC64-BUFFER'], bytearray([0, 255, 42]))
        address, size, _ = p.symbols['destination']
        self.assertEqual(p.cpu.memory[address:address+size], [0, 255, 42, 0, 0, 0, 0, 0])
        p.restart()
        p.run()
        self.assertEqual([p.value(n) for n in ('written', 'received', 'write_ok', 'read_ok', 'dos')],
                         [0, 0, 0, 0, 63])
        self.assertEqual(p.disk.files[b'PYC64-BUFFER'], bytearray([0, 255, 42]))

    def test_disk_read_into_counts_tail_and_empty_noop(self):
        p = self.compile('''import c64.disk as disk
values = [99, 99, 99, 99]
opened = disk.open(2, 8, 2, "data,s,r")
first = disk.read_into(2, values, 9)
first_eof = disk.eof()
second = disk.read_into(2, values, 9)
second_eof = disk.eof()
noop = disk.read_into(2, values, 0)
print(first, first_eof, second, second_eof, noop, values[0], values[1], values[2], values[3])
disk.close(2)
opened_empty = disk.open(2, 8, 2, "empty,s,r")
empty_count = disk.read_into(2, values, 0)
disk.close(2)
''')
        p.disk = DiskKernal()
        p.disk.files[b'DATA'] = bytearray([0, 255, 42, 99, 7, 8])
        p.disk.files[b'EMPTY'] = bytearray()
        p.run()
        self.assertEqual(p.output, b'4 FALSE 2 TRUE 0 7 8 42 99\r')
        self.assertEqual(p.disk.read_count, 6)
        self.assertEqual(p.value('empty_count'), 0)
        self.assertIsNone(p.disk.input)
        self.assertIsNone(p.disk.output)

    def test_disk_buffer_transport_errors_and_partial_progress(self):
        p = self.compile('''import c64.disk as disk
values = [99, 99, 99, 99]
opened = disk.open(2, 8, 2, "data,s,r")
count = disk.read_into(2, values, 4)
failed = disk.failed()
status = disk.status()
print(count, failed, status, values[0], values[1], values[2], values[3])
''')
        p.disk = DiskKernal()
        p.disk.files[b'DATA'] = bytearray([10, 20, 30, 40])
        p.disk.read_status = 2
        p.disk.read_error_after = 3
        p.run()
        self.assertEqual(p.output, b'2 TRUE 2 10 20 99 99\r')
        p = self.compile('''import c64.disk as disk
values = [10, 20, 30, 40]
opened = disk.open(2, 8, 2, "data,s,w")
count = disk.write_from(2, values, 4)
failed = disk.failed()
status = disk.status()
print(count, failed, status)
''')
        p.disk = DiskKernal()
        p.disk.write_status = 1
        p.disk.write_error_after = 3
        p.run()
        self.assertEqual(p.output, b'2 TRUE 1\r')
        self.assertEqual(p.disk.files[b'DATA'], bytearray([10, 20, 30]))
        self.assertEqual(p.disk.write_count, 3)
        self.assertIsNone(p.disk.input)
        self.assertIsNone(p.disk.output)

    def test_disk_write_from_byte_validation_and_zero_counts(self):
        for values, expected, count in [('[10, 20, 256, 40]', [10, 20], 2),
                                        ('[-1, 20]', [], 0),
                                        ('[0, 255, 42]', [0, 255, 42], 3)]:
            with self.subTest(values=values):
                p = self.compile(f'''import c64.disk as disk
values = {values}
opened = disk.open(2, 8, 2, "data,s,w")
noop = disk.write_from(2, values, 0)
count = disk.write_from(2, values, 100)
disk.close(2)
''')
                p.disk = DiskKernal()
                p.run()
                self.assertEqual(p.value('noop'), 0)
                self.assertEqual(p.value('count'), count)
                self.assertEqual(p.disk.write_count, count)
                # The real 1541 inserts CR when closing a new SEQ with no data.
                self.assertEqual(p.disk.files[b'DATA'], bytearray(expected or [13]))

    def test_collections_and_disk_buffers_under_cpython(self):
        script = '''import sys, types
from pyc64.types import array, u8, u16
from pyc64.collections import fill, copy_into
def annotated(buffer: array[u8]):
    return len(buffer)
kernal = types.ModuleType("c64.kernal")
state = {"status": 0, "position": 0, "writes": [], "reads": 0}
data = [0, 255, 42]
def read_byte(logical):
    value = data[state["position"]]
    state["position"] += 1
    state["reads"] += 1
    state["status"] = 64 if state["position"] == len(data) else 0
    return value
def write_byte(logical, value):
    state["writes"].append(value)
    state["status"] = 0
    return True
for name in ("open", "close", "write_text"):
    setattr(kernal, name, lambda *args: True)
kernal.read_byte = read_byte
kernal.write_byte = write_byte
kernal.status = lambda: state["status"]
kernal.error = lambda: 0
sys.modules["c64.kernal"] = kernal
import c64.disk as disk
target = array(u8, 8)
assert annotated(target) == 8
try:
    array[u8, None]
except ValueError:
    pass
else:
    raise AssertionError("explicit array dimensions must be integers")
fill(target, u8(99))
assert disk.write_from(2, data, 3) == 3
assert disk.read_into(3, target, 3) == 3
assert disk.eof() and not disk.failed()
assert [target[i] for i in range(8)] == [0, 255, 42, 99, 99, 99, 99, 99]
assert disk.read_into(3, target, 0) == 0 and state["reads"] == 3
assert state["writes"] == data
wide = array(u16, 5)
assert copy_into(data, wide, 9) == 3
assert [wide[i] for i in range(5)] == [0, 255, 42, 0, 0]
assert disk.write_from(2, [10, 256, 42], 3) == 1
'''
        result = subprocess.run([sys.executable, '-c', script], cwd=ROOT / 'stdlib',
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_container_parameters_typed_lists_and_aliases(self):
        p = self.compile('''def edit(words: list[str], flags: list[bool], numbers: list[u16]):
    words[-1] = "changed"
    flags[0] = not flags[1]
    numbers[-1] = numbers[0] + 300
def alias(first: list[u16], second: list[u16]):
    first[0] = 500
    second[1] = first[0] + 1
words = ["first", "last"]
flags = [False, False]
numbers: list[u16] = [10, 20, 30]
edit(words, flags, numbers)
alias(numbers, numbers)
print(words[0], words[1], flags[0], numbers[0], numbers[1], numbers[-1])
''')
        p.run()
        self.assertEqual(p.output, b'FIRST CHANGED TRUE 500 501 310\r')
        self.assertEqual(p.symbols['edit.words'][1], 4)
        self.assertEqual(p.symbols['alias.second'][1], 4)
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
        p.output.clear()
        p.restart()
        p.run()
        self.assertEqual(p.output, b'FIRST CHANGED TRUE 500 501 310\r')

    def test_container_parameters_inference_lengths_and_forwarding(self):
        p = self.compile('''def outer(values):
    return inner(values)
def inner(values):
    values[-1] = values[0]
    return len(values)
def size(values: list[u8]) -> u16:
    return len(values)
short = [10]
long = [20, 30, 40]
empty = []
first = outer(short)
second = outer(long)
nothing = size(empty)
print(first, second, nothing, long[2])
''')
        p.run()
        self.assertEqual(p.output, b'1 3 0 20\r')
        self.assertEqual(p.symbols['outer.values'][1], 4)
        # Layout inference must converge before exact reference types are checked.
        p = self.compile('''def echo(value):
    return value
def head(values):
    return values[0]
values = [echo(1), echo(500)]
result = head(values)
print(result, values[1])
''')
        p.run()
        self.assertEqual(p.output, b'1 500\r')

    def test_container_parameters_arrays_pages_and_constant_annotations(self):
        self.module('helpers.py', '''COUNT: Final[u16] = 300
def edit(values: array[u16, COUNT]) -> u16:
    values[255] = 1234
    values[256] = values[255] + 1
    values[299] = 65535
    return len(values)
def forward(values: array[u16]):
    return edit(values)
''')
        self.module('exports.py', 'from helpers import forward as modify\n')
        p = self.compile('''from exports import modify
values: array[u16, 300] = array(u16, 300)
count = modify(values)
print(count, values[255], values[256], values[299])
''')
        p.run()
        self.assertEqual(p.output, b'300 1234 1235 65535\r')
        self.assertEqual(p.symbols['helpers.edit.values'][1], 4)

    def test_container_parameters_argument_snapshots_and_local_reset(self):
        p = self.compile('''def change(values, value: u8) -> u16:
    values[0] = value
    return len(values)
first = [1]
second = [2, 3]
result = change(first, u8(change(second, 7)))
def local(value: u8) -> u8:
    values = [1, 2]
    change(values, value)
    return values[0] + values[1]
one = local(8)
two = local(20)
print(result, first[0], second[0], one, two)
''')
        p.run()
        self.assertEqual(p.output, b'1 2 7 10 22\r')

    def test_container_parameters_runtime_bounds_and_shape_guards(self):
        for storage in ['values = [10, 20]\n', 'values: array[u8, 2] = array(u8, 2)\n']:
            for index in [-3, 2, 65535]:
                with self.subTest(storage=storage, index=index):
                    p = self.compile('def get(values, index):\n    return values[index]\n' +
                                     storage + f'result = get(values, {index})\n')
                    with self.assertRaisesRegex(RuntimeError, 'BRK'):
                        p.run()
        p = self.compile('''def fixed(values: array[u8, 3]):
    values[0] = 99
def forward(values):
    fixed(values)
values = array(u8, 2)
forward(values)
''')
        with self.assertRaisesRegex(RuntimeError, 'BRK'):
            p.run()
        address, size, _ = p.symbols['values']
        self.assertEqual(p.cpu.memory[address:address+size], [0, 0])
        for index in [-1, -2]:
            p = self.compile('def get(values, index):\n    return values[index]\n'
                             'values = [10, 20]\n' + f'result = get(values, {index})\n')
            p.run()
            self.assertEqual(p.value('result'), [20, 10][-index-1])

    def test_indexed_assignment_python_evaluation_order(self):
        text = '''def right(values) -> u16:
    print("right")
    values[0] = 1
    return 500
def index(values) -> u8:
    print("index")
    return u8(values[0])
values: list[u16] = [0, 10]
values[index(values)] = right(values)
print(values[0], values[1])
'''
        p = self.compile(text)
        p.run()
        self.assertEqual(p.output, b'RIGHT\rINDEX\r1 500\r')
        result = subprocess.run([sys.executable, '-c', library_bindings(text)],
                                cwd=ROOT / 'stdlib', text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.upper().replace('\n', '\r').encode(), p.output)
        p = self.compile('''def right() -> u8:
    print("evaluated")
    return 42
def write(values, index: i16):
    values[index] = right()
values = [10, 20]
write(values, 2)
''')
        with self.assertRaisesRegex(RuntimeError, 'BRK'):
            p.run()
        self.assertEqual(p.output, b'EVALUATED\r')

    def test_container_parameter_restrictions(self):
        cases = [
            ('def f(values: list[u16]):\n    pass\nvalues = [1, 2]\nf(values)\n', 'must match exactly'),
            ('def f(values):\n    print(len(values))\na = [1]\nb = [500]\nf(a)\nf(b)\n', 'must match exactly'),
            ('def f(values: list[u8]):\n    pass\nvalues = array(u8, 2)\nf(values)\n', 'requires list'),
            ('def f(values: array[u8, 3]):\n    pass\nvalues = array(u8, 2)\nf(values)\n', 'expects 3 elements'),
            ('def f(values: array[u8, 0]):\n    pass\n', '1..16384'),
            ('def f(values: array[str, 3]):\n    pass\n', 'scalar element types'),
            ('def f(values: list[u8]):\n    values = [3, 4]\nvalues = [1, 2]\nf(values)\n', 'rebinding a container parameter'),
            ('def f(values: array[u8, 2]):\n    values = array(u8, 2)\nvalues = array(u8, 2)\nf(values)\n', 'rebinding a container parameter'),
            ('def f(values: list[u8]):\n    pass\nf([1, 2])\n', 'before passing'),
            ('def f(values):\n    return len(values)\nvalues = []\nf(values)\n', 'cannot infer container element type'),
            ('def f(values):\n    return values\nvalues = array(u8, 2)\nf(values)\n', 'return values are unsupported'),
        ]
        for text, message in cases:
            with self.subTest(text=text):
                self.reject(text, message)

    def test_container_parameter_hardware_stack_budget(self):
        parameters = ', '.join(f'p{i}' for i in range(15))
        arguments = ', '.join('values' for _ in range(15))
        definitions = 'def f0() -> u8:\n    return 1\n' + ''.join(
            f'def f{i}() -> u8:\n    return f{i-1}()\n' for i in range(1, 36))
        self.reject(definitions + f'def pick({parameters}, value: u8) -> u8:\n    return value\n'
                    'values = [1]\n' + f'result = pick({arguments}, f35())\n', 'hardware stack')
        p = self.compile(f'def pick({parameters}, value: u8) -> u8:\n    return p14[0] + value\n'
                         'values = [10]\n' + f'result = pick({arguments}, 1)\n')
        p.run()
        self.assertEqual(p.value('result'), 11)

    def cli(self, *args, cwd=None, env=None):
        return subprocess.run([str(COMPILER), *map(str, args)], cwd=cwd or self.dir,
                              env=env, text=True, capture_output=True)

    def project(self, name='demo', extra='', entry='main.py', text='print("Project works")\n'):
        self.module(entry, text)
        path = self.dir / 'pyc64.toml'
        path.write_text(f'[project]\nname = "{name}"\nentry = "{entry}"\n' + extra)
        return path

    def test_cli_new_project_workflow(self):
        result = self.cli('new', 'space-game')
        self.assertEqual(result.returncode, 0, result.stderr)
        root = self.dir / 'space-game'
        config = tomllib.loads((root / 'pyc64.toml').read_text())
        self.assertEqual(config['project']['name'], 'space-game')
        ast.parse((root / 'main.py').read_text())
        for command in ['check', 'size']:
            result = self.cli(command, cwd=root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((root / 'build').exists())
        result = self.cli(cwd=root)
        self.assertEqual(result.returncode, 0, result.stderr)
        output = root / 'build/space-game.PRG'
        self.assertTrue(output.exists())
        self.assertTrue((root / 'build/space-game.map').exists())
        p = Program(output.read_bytes(), {})
        p.run()
        self.assertEqual(p.output, b'HELLO FROM PYC64!\r')
        previous = {p.name: p.read_bytes() for p in root.iterdir() if p.is_file()}
        result = self.cli('new', 'space-game')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual({p.name: p.read_bytes() for p in root.iterdir() if p.is_file()}, previous)

    def test_cli_new_restrictions_and_missing_project(self):
        for name in ['../outside', '.', '..', 'CON', 'nul', 'COM1', 'LPT9',
                     'bad.name', 'two words', 'a'*64]:
            with self.subTest(name=name):
                result = self.cli('new', name)
                self.assertNotEqual(result.returncode, 0)
        for args in [('new',), ('new', 'game', '-o', 'out.PRG'),
                     ('clean', 'main.py'), ('clean', '-I', '.'),
                     ('check', '--vice', 'vice'), ('build', '--project'),
                     ('run', '--vice', ''), ('--unexpected',)]:
            with self.subTest(args=args):
                result = self.cli(*args)
                self.assertNotEqual(result.returncode, 0)
        result = self.cli('build')
        self.assertIn('cannot open project', result.stderr)
        self.assertFalse((self.dir / 'build').exists())
        result = self.cli('--version')
        self.assertEqual(result.stdout.strip(), 'PyC64 0.1.0')
        self.assertEqual(result.returncode, 0)

    def test_cli_project_relative_paths_and_overrides(self):
        config = self.project(name='space-game', entry='sources/main.py',
                              text='from pyc64.math import clamp_u8\nprint(clamp_u8(10, 0, 5))\n',
                              extra='[target]\nmachine = "c64-reu"\n[build]\n'
                                    'directory = "output/c64"\noptimize = "size"\n')
        self.module('sources/helper.py', 'value = 7\n')
        for command in ['build', 'asm', 'size', 'check']:
            result = self.cli(command, '--project', config, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stderr)
        root = self.dir / 'output/c64'
        p = Program((root / 'space-game.PRG').read_bytes(), {})
        p.run()
        self.assertEqual(p.output, b'5\r')
        self.assertIn('jsr', (root / 'space-game.lst').read_text())
        alternate = self.dir / 'custom.PRG'
        result = self.cli('build', '--target', 'c64', '-o', alternate,
                          '--map', self.dir / 'custom.map')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(alternate.read_bytes(), (root / 'space-game.PRG').read_bytes())
        result = self.cli('build', self.dir / 'sources/main.py', '--project', config)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not both', result.stderr)
        # An explicit source preserves the existing standalone workflow even
        # when an unrelated project file is present in the current directory.
        config.write_text('invalid TOML')
        result = self.cli('build', self.dir / 'sources/main.py')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.dir / 'sources/main.PRG').exists())

    def test_cli_project_toml_diagnostics(self):
        config = self.project()
        valid = config.read_text()
        cases = [('', 'project.name is required'),
                 ('[project]\nname = "demo"\nname = "again"', 'duplicate project key'),
                 (valid + '[project]\n', 'duplicate project table'),
                 (valid + 'unknown = "ignored"\n', 'unsupported project key'),
                 (valid + '[unsupported]\ndirectory = "assets"', 'unsupported project table'),
                 (valid + '[assets]\ndirectory = ""', 'assets.directory must not be empty'),
                 ('[project]\nname = 10', 'single-line TOML string'),
                 ('[project]\nname = "demo" trailing', 'single-line TOML string'),
                 ('[project]\nname = "../bad"', 'project name'),
                 (valid + '[target]\nmachine = "amiga"', 'unknown target'),
                 (valid + '[build]\noptimize = "fast"', 'only build.optimize'),
                 (valid + '[build]\ndirectory = "../outside"', 'child directory'),
                 (valid + '[build]\ndirectory = "/tmp"', 'child directory'),
                 (valid + '[build]\ndirectory = "C:/outside"', 'child directory'),
                 (valid + '[build]\ndirectory = "."', 'child directory'),
                 (valid + '[build]\ndirectory = "build/.."', 'child directory'),
                 (valid + '[build]\ndirectory = "build. "', 'child directory'),
                 (valid + '[emulator]\nexecutable = ""', 'must not be empty'),
                 (valid + '\0', 'NUL byte'),
                 ('#' + 'a'*17000, '16384 bytes')]
        for text, expected in cases:
            with self.subTest(text=text[:100]):
                config.write_text(text)
                result = self.cli('build')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stderr)
                self.assertRegex(result.stderr, r'pyc64\.toml:\d+: error:')
                self.assertFalse((self.dir / 'build').exists())
        config.write_text("# TOML comments\n[project] # name\nname='demo' # comment\n"
                          "entry = 'main.py'\n[build]\ndirectory = './build/nested'\n")
        self.assertEqual(tomllib.loads(config.read_text())['project']['name'], 'demo')
        result = self.cli('build')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cli_clean_preserves_other_files_and_sources(self):
        config = self.project()
        for command in ['build', 'asm']:
            result = self.cli(command)
            self.assertEqual(result.returncode, 0, result.stderr)
        keep = self.dir / 'build/other.PRG'
        keep.write_bytes(b'other program')
        nested = self.dir / 'build/assets/data.bin'
        nested.parent.mkdir()
        nested.write_bytes(b'asset')
        source = (self.dir / 'main.py').read_bytes()
        configuration = config.read_bytes()
        for _ in range(2):
            result = self.cli('clean', '--project', config, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stderr)
        for extension in ['PRG', 'map', 'lst']:
            self.assertFalse((self.dir / f'build/demo.{extension}').exists())
        self.assertEqual(keep.read_bytes(), b'other program')
        self.assertEqual(nested.read_bytes(), b'asset')
        self.assertEqual((self.dir / 'main.py').read_bytes(), source)
        self.assertEqual(config.read_bytes(), configuration)

    def test_cli_project_output_protection(self):
        config = self.project()
        before = config.read_bytes()
        for flag in ['-o', '--map']:
            result = self.cli('build', flag, config)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('project configuration', result.stderr)
            self.assertEqual(config.read_bytes(), before)
        output = self.dir / 'build/demo.PRG'
        self.assertEqual(self.cli('build').returncode, 0)
        previous = output.read_bytes()
        (self.dir / 'main.py').write_text('print(missing)\n')
        result = self.cli('build')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), previous)
        (self.dir / 'build/demo.PRG').unlink()
        (self.dir / 'build/demo.PRG').mkdir()
        result = self.cli('clean')
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(output.is_dir())

    def test_cli_run_build_failure_and_emulator_selection(self):
        self.project(extra="[emulator]\nexecutable = 'missing-configured-vice'\n")
        env = os.environ.copy()
        env.pop('PYC64_VICE', None)
        result = self.cli('run', env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Launching missing-configured-vice', result.stdout)
        self.assertIn('cannot launch VICE', result.stderr)
        self.assertTrue((self.dir / 'build/demo.PRG').exists())
        env['PYC64_VICE'] = 'missing-environment-vice'
        result = self.cli('run', env=env)
        self.assertIn('Launching missing-environment-vice', result.stdout)
        result = self.cli('run', '--vice', 'missing-option-vice', env=env)
        self.assertIn('Launching missing-option-vice', result.stdout)
        # Copying the compiler also verifies an executable path with spaces is
        # passed as a single process argument, without command-shell parsing.
        executable = self.dir / ('vice with spaces.exe' if os.name == 'nt' else 'vice with spaces')
        shutil.copy2(COMPILER, executable)
        result = self.cli('run', '--vice', executable)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown option '-autostart'", result.stderr)
        (self.dir / 'main.py').write_text('print(missing)\n')
        result = self.cli('run', '--vice', executable)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('Launching', result.stdout)

    def test_cli_file_aliases_and_build_directory_links(self):
        config = self.project()
        result = self.cli('build', '--map', self.dir / './pyc64.toml')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('project configuration', result.stderr)
        alias = self.dir / 'configuration.map'
        try:
            os.link(config, alias)
        except PermissionError:
            # Windows sandbox profiles may disallow creating hard links.
            pass
        else:
            result = self.cli('build', '--map', alias)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('project configuration', result.stderr)
        source = self.dir / 'main.py'
        original = source.read_bytes()
        output = self.dir / 'main.PRG'
        result = self.cli('build', source, '-o', self.dir / './main.py')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(source.read_bytes(), original)
        try:
            os.link(source, output)
        except PermissionError:
            pass
        else:
            result = self.cli('build', source)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(source.read_bytes(), original)
        if os.name != 'nt':
            outside = self.dir / 'outside'
            outside.mkdir()
            (outside / 'demo.PRG').write_bytes(b'keep')
            (self.dir / 'build').symlink_to(outside, target_is_directory=True)
            for command in ['build', 'clean']:
                result = self.cli(command)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('link or junction', result.stderr)
            self.assertEqual((outside / 'demo.PRG').read_bytes(), b'keep')

    def test_inferred_parameters_and_string_lists_example(self):
        p = self.compile((ROOT / 'examples/lists/main.py').read_text())
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'YES INDEED\rANSWER: MAYBE COUNT: 4\r'
                                  b'DOUBLED: 24 600\rROTATED: 30 10 20\r')
        self.assertEqual([p.value(n) for n in ('small', 'large')], [24, 600])
        self.assertEqual(p.symbols['doubled.value'][2], 'u16')
        self.assertEqual(p.symbols['kevinallen.peanut_butter'][2], 'str')
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
        p.restart()
        p.output.clear()
        p.run()
        self.assertEqual(p.value('large'), 600)
        self.assertIn(b'YES INDEED', p.output)
        script = '''import runpy, sys, types
sys.path.insert(0, "stdlib")
video = types.ModuleType("c64.video")
video.screen = types.SimpleNamespace(clear=lambda: None)
sys.modules["c64.video"] = video
runpy.run_path(sys.argv[1])
'''
        result = subprocess.run([sys.executable, '-c', script, str(ROOT / 'examples/lists/main.py')],
                                cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(p.output, result.stdout.upper().replace('\n', '\r').encode())

    def test_explicit_none_returns_match_python_control_flow(self):
        self.iteration_oracle('''class Counter:
    _value: u8
    def __init__(self):
        self._value = 1
        return None
    @property
    def value(self) -> u8:
        return self._value
    @value.setter
    def value(self, value: u8) -> None:
        self._value = value
        return (None)
        self._value = 99
def update(values, stop):
    if stop:
        return None
    values[0] = values[0] + 1
def explicit(values: list[u8]) -> None:
    values[0] = values[0] + 2
    return (None)
    values[0] = values[0] + 20
values = [1]
update(values, True)
update(values, False)
explicit(values)
counter = Counter()
counter.value = values[0]
print(values[0], counter.value)
''')
        self.reject('def f() -> u8:\n    return None\nf()\n', 'return value required')
        self.reject('return None\n', 'return outside a function')
        self.reject('def f():\n    return None + 1\nf()\n', 'None')

    def test_inference_all_calls_and_forward_propagation(self):
        for calls in [('small = outer(5)', 'large = outer(500)'),
                      ('large = outer(500)', 'small = outer(5)')]:
            with self.subTest(calls=calls):
                p = self.compile('''def outer(value):
    local = inner(value)
    return local
def inner(value):
    return value + 1
''' + '\n'.join(calls) + '\n')
                p.run()
                self.assertEqual([p.value(n) for n in ('small', 'large')], [6, 501])
                self.assertEqual(p.symbols['outer.value'][2], 'u16')
                self.assertEqual(p.symbols['inner.value'][2], 'u16')
        definitions = ''.join(f'def f{i}(value):\n    return f{i+1}(value)\n' for i in range(24))
        p = self.compile(definitions + 'def f24(value):\n    return value\nresult = f0(500)\n')
        p.run()
        self.assertEqual(p.value('result'), 500)

    def test_inference_mixed_annotations_signed_and_bool(self):
        p = self.compile('''def combine(first, second: u16) -> u16:
    return first + second
def identity(value):
    return value
def positive(value):
    if value > 0:
        return True
    else:
        return False
combined = combine(10, 500)
wide = combine(400, 500)
negative = identity(-250)
positive_value = identity(250)
yes = positive(5)
no = positive(-5)
''')
        p.run()
        self.assertEqual([p.value(n) for n in ('combined', 'wide', 'negative', 'positive_value', 'yes', 'no')],
                         [510, 900, -250, 250, 1, 0])
        self.assertEqual(p.symbols['identity.value'][2], 'i16')
        self.assertEqual(p.symbols['yes'][2], 'bool')

    def test_inference_imports_and_list_elements(self):
        self.module('helpers.py', '''words = ["first", "second"]
def echo(value):
    return value
def selected():
    return words[-1]
''')
        self.module('exports.py', 'from helpers import echo as repeat, selected\n')
        p = self.compile('from exports import repeat, selected\n'
                         'print(repeat(selected()))\n')
        p.run()
        self.assertEqual(p.output, b'SECOND\r')
        self.assertEqual(p.symbols['helpers.echo.value'][2], 'str')

    def test_inference_diagnostics(self):
        cases = [
            ('def unused(value):\n    print(value)\n', "cannot infer parameter 'value'"),
            ('def echo(value):\n    return value\necho(1)\necho("x")\n', 'incompatible inferred types'),
            ('def echo(value):\n    return value\necho(True)\necho(1)\n', 'incompatible inferred types'),
            ('def echo(value):\n    return value\necho(-1)\necho(65535)\n', 'incompatible inferred types'),
            ('def echo(value):\n    return value\necho(1, 2)\n', 'expects 1 arguments'),
            ('def recurse(value):\n    return recurse(value)\nrecurse(1)\n', 'recursion'),
            ('def first(value):\n    return second(value)\ndef second(value):\n'
             '    return first(value)\nfirst(1)\n', 'recursion'),
            ('def sometimes(value):\n    if value:\n        return 1\nsometimes(True)\n',
             'must return on every path'),
            ('def mixed(value):\n    if value:\n        return 1\n    else:\n        return\nmixed(True)\n',
             'incompatible inferred types'),
            ('def echo(value):\n    return value\necho(print("x"))\n', 'no return value'),
            ('def echo(value):\n    return value\nvalues = [1, 2]\necho(values)\n', 'list/array return values'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.reject(source, error)

    def test_fixed_list_numeric_bool_and_annotations(self):
        p = self.compile('''values = [1, 500, 65535]
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
''')
        p.run()
        self.assertEqual(p.output, b'1 500 300 -5 250\rTRUE TRUE BLUE 0 0\r')
        self.assertEqual(p.symbols['values'][1:], (6, 'list'))
        self.assertEqual(p.symbols['signed'][1:], (6, 'list'))
        self.assertEqual(p.symbols['flags'][1:], (2, 'list'))
        p = self.compile('''values = [
    10,
    20,
    30,
]
total: u16 = 0
for index in range(len(values)):
    total += values[index]
''')
        p.run()
        self.assertEqual(p.value('total'), 60)

    def test_list_initialization_order_and_reassignment_snapshot(self):
        p = self.compile('''values = [10, 20]
def item(value):
    print(value)
    values[0] = 99
    return value
values = [item(values[1]), values[0]]
print(values[0], values[1])
''')
        p.run()
        self.assertEqual(p.output, b'20\r20 99\r')
        p = self.compile('''def choose(value):
    local = [value, value + 1]
    return local[-1]
first = choose(3)
second = choose(300)
''')
        p.run()
        self.assertEqual([p.value(n) for n in ('first', 'second')], [4, 301])

    def test_list_index_guards(self):
        for index, expected in [(-3, 10), (-1, 30), (0, 10), (2, 30)]:
            p = self.compile(f'values = [10, 20, 30]\nindex: i16 = {index}\nresult = values[index]\n')
            p.run()
            self.assertEqual(p.value('result'), expected)
        for index, kind in [(-4, 'i16'), (3, 'u8'), (65535, 'u16')]:
            for action in ('result = values[index]', 'values[index] = 99'):
                p = self.compile(f'values = [10, 20, 30]\nindex: {kind} = {index}\n{action}\n')
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                address, size, _ = p.symbols['values']
                self.assertEqual(p.cpu.memory[address:address+size], [10, 20, 30])
        self.reject('values = [1, 2]\nprint(values[-3])\n', 'list index is out of bounds')
        self.reject('values = [1, 2]\nprint(values[2])\n', 'list index is out of bounds')

    def test_list_restriction_diagnostics(self):
        cases = [
            ('values = [1, "x"]\n', 'incompatible inferred types'),
            ('values = [True, 1]\n', 'incompatible inferred types'),
            ('values = [[1]]\n', 'list elements'),
            ('values = [1]\nother = values\n', 'list aliases'),
            ('values = [1]\nvalues = [1, 2]\n', 'list size cannot change'),
            ('values = ["x"]\nvalues[0] = 1\n', 'cannot assign'),
            ('values = [1]\nvalues[0] = 300\n', 'does not fit in u8'),
            ('values = [values[0]]\n', 'before initialization'),
            ('values = []\nprint(values[0])\n', 'list index is out of bounds'),
            ('print(len([1, 2]))\n', 'assign the list literal'),
            ('print([1, 2][0])\n', 'assign the list literal'),
            ('values = [1]\nvalues.append(2)\n', 'unknown function'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.reject(source, error)

    def test_disk_text_output_and_pointer_carry(self):
        for text in ('', 'hello\nworld', 'x' * 700):
            with self.subTest(length=len(text)):
                p = self.compile('''from c64.disk import open, close, write_text
def filename() -> str:
    close(4)
    return "text,s,w"
def contents() -> str:
    close(5)
    return ''' + repr(text).replace("'", '"') + '''
open(2, 8, 2, filename())
written: bool = write_text(2, contents())
close(2)
print("screen")
''')
                p.disk = DiskKernal()
                p.run()
                # Closing an unwritten new SEQ causes 1541 DOS to insert CR.
                expected = text.upper().replace('\n', '\r').encode()
                self.assertEqual(p.disk.files[b'TEXT'], expected or b'\r')
                self.assertEqual(p.value('written'), 1)
                self.assertEqual(p.output, b'SCREEN\r')
                self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
                self.assertEqual(p.disk.closed, [4, 5, 2])

    def test_disk_text_errors_and_channel_restoration(self):
        for missing, flush_error, during_error in [(True, 0, 0), (False, 1, 0), (False, 0, 1)]:
            with self.subTest(missing=missing, flush=flush_error, during=during_error):
                p = self.compile('''import c64.disk as disk
''' + ('' if missing else 'disk.open(2, 8, 2, "text,s,w")\n') + '''
written: bool = disk.write_text(2, "abcd")
saved: u8 = disk.status()
error: u8 = disk.error()
print("screen")
''')
                p.disk = DiskKernal()
                p.disk.flush_status = flush_error
                p.disk.write_status = during_error
                p.disk.write_error_after = 2 if during_error else None
                p.run()
                self.assertEqual(p.value('written'), 0)
                self.assertEqual(p.value('saved'), flush_error or during_error)
                self.assertEqual(p.value('error'), 3 if missing else 0)
                self.assertEqual(p.output, b'SCREEN\r')
                if during_error:
                    self.assertEqual(p.disk.files[b'TEXT'], b'AB')
                self.assertIsNone(p.disk.output)
        self.reject('from c64.disk import write_text\nwrite_text(2, 42)\n', 'str')
        self.reject('from c64.disk import write_text\nwrite_text(2)\n', 'expects 2 arguments')

    def test_disk_dos_codes_and_complete_records(self):
        p = self.compile('''import c64.disk as disk
disk.open(15, 8, 15, "")
code: u8 = disk.dos_code(15)
transport_failed: bool = disk.failed()
next_code: u8 = disk.dos_code(15)
disk.close(15)
''')
        for code in range(100):
            with self.subTest(code=code):
                p.restart()
                p.disk = DiskKernal()
                record = f'{code:02},MESSAGE,12,34\r'.encode()
                p.disk.responses = [record, b'00,OK,00,00\r']
                p.run()
                self.assertEqual(p.value('code'), code)
                self.assertEqual(p.value('next_code'), 0)
                self.assertEqual(p.value('transport_failed'), 0)
                self.assertEqual(p.disk.read_count, len(record) + 12)

    def test_disk_dos_malformed_and_bounded_records(self):
        records = [b'X0,OK,00,00\r', b'001,OK,00,00\r', b'0,OK,00,00\r', b'00,,00,00\r',
                   b'00,OK,X0,00\r', b'00,OK,00,X0\r', b'00,OK,0,00\r', b'00,OK,00,000\r',
                   b'00,OK,00,00', b'00,OK,00,00\x00\r', b'00,OK,00,00,\r',
                   b'00,OK\r', b'00,\x01,00,00\r', b'\r', b'X' * 80]
        p = self.compile('''from c64.disk import open, close, dos_code
open(15, 8, 15, "")
code: u8 = dos_code(15)
close(15)
''')
        for record in records:
            with self.subTest(record=record):
                p.restart()
                p.disk = DiskKernal()
                p.disk.responses = [record]
                p.run()
                self.assertEqual(p.value('code'), 255)
                self.assertEqual(p.disk.read_count, min(len(record), 64))
        p.restart()
        p.disk = DiskKernal()
        p.disk.responses = [b'99,' + b'M' * 54 + b',00,00\r']
        p.run()
        self.assertEqual(p.value('code'), 99)
        self.assertEqual(p.disk.read_count, 64)
        p.restart()
        p.disk = DiskKernal()
        p.disk.responses = [b'00,OK,00,00\rtrailing']
        p.run()
        self.assertEqual(p.value('code'), 0)
        self.assertEqual(p.disk.read_count, 12)

    def test_disk_dos_transport_errors_and_commands(self):
        for status in (1, 2, 128):
            p = self.compile('''import c64.disk as disk
disk.open(15, 8, 15, "")
code: u8 = disk.dos_code(15)
''')
            p.disk = DiskKernal()
            p.disk.read_status = status
            p.run()
            self.assertEqual(p.value('code'), 255)
            self.assertEqual(p.disk.read_count, 1)
        p = self.compile('''import c64.disk as disk
missing: u8 = disk.dos_code(7)
disk.open(15, 8, 15, "")
code: u8 = disk.command(15, "r0:new=old")
disk.close(15)
print("command:", code)
''')
        p.disk = DiskKernal()
        p.disk.responses = [b'62,FILE NOT FOUND,00,00\r']
        p.run()
        self.assertEqual(p.disk.commands, [b'R0:NEW=OLD'])
        self.assertEqual(p.value('missing'), 255)
        self.assertEqual(p.value('code'), 62)
        self.assertEqual(p.output, b'COMMAND: 62\r')
        p.restart()
        p.disk = DiskKernal()
        p.disk.flush_status = 1
        p.run()
        self.assertEqual(p.value('code'), 255)
        self.assertEqual(p.disk.read_count, 0)

    def test_disk_binary_round_trip(self):
        p = self.compile((ROOT / 'examples/disk/main.py').read_text())
        p.disk = DiskKernal()
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.disk.files[b'PYC64-DEMO'], b'\x00\xff\x2a')
        self.assertEqual([p.value(n) for n in ('written', 'read_ok', 'count', 'total', 'last')],
                         [1, 1, 3, 297, 42])
        self.assertIn(b'DISK: TRUE TRUE\rBYTES: 3 SUM: 297\r', p.output)
        self.assertFalse(p.disk.channels)
        self.assertIsNone(p.disk.input)
        self.assertIsNone(p.disk.output)
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])

    def test_disk_commands_example_and_existing_file(self):
        p = self.compile((ROOT / 'examples/disk_commands/main.py').read_text())
        p.disk = DiskKernal()
        p.run()
        self.assertEqual(p.disk.files, {b'PYC64-RENAMED': b'HELLO'})
        self.assertEqual([p.value(n) for n in ('created', 'renamed', 'missing', 'read_ok', 'value')],
                         [1, 0, 62, 1, 72])
        self.assertEqual(p.disk.commands, [b'R0:PYC64-RENAMED=PYC64-DEMO'])
        self.assertFalse(p.disk.channels)
        p = self.compile((ROOT / 'examples/disk/main.py').read_text())
        p.disk = DiskKernal()
        p.disk.files[b'PYC64-DEMO'] = bytearray(b'original')
        p.run()
        self.assertEqual(p.disk.files[b'PYC64-DEMO'], b'original')
        self.assertEqual(p.value('written'), 0)
        self.assertEqual(p.value('dos'), 63)
        self.assertFalse(p.disk.channels)

    def test_disk_final_byte_and_saved_status(self):
        p = self.compile('''import c64.disk as disk
initial: u8 = disk.status()
initial_error: u8 = disk.error()
opened: bool = disk.open(3, 8, 3, "data,s,r")
first: u8 = disk.read_byte(3)
first_eof: bool = disk.eof()
last: u8 = disk.read_byte(3)
print("screen")
eof: bool = disk.eof()
failed: bool = disk.failed()
status: u8 = disk.status()
error: u8 = disk.error()
disk.close(3)
closed_status: u8 = disk.status()
''')
        p.disk = DiskKernal()
        p.disk.files[b'DATA'] = bytearray([0, 255])
        p.run()
        self.assertEqual([p.value(n) for n in ('initial', 'initial_error', 'opened', 'first',
                                             'first_eof', 'last', 'eof', 'failed', 'status',
                                             'error', 'closed_status')],
                         [0, 0, 1, 0, 0, 255, 1, 0, 64, 0, 0])
        self.assertEqual(p.output, b'SCREEN\r')
        p.restart()
        p.run()
        self.assertEqual(p.value('initial'), 0)
        self.assertEqual(p.value('initial_error'), 0)

    def test_disk_open_and_channel_errors(self):
        for source, setup, expected in [
            ('disk.open(2, 9, 2, "data,s,r")', 0, (0, 5, 128)),
            ('disk.open(2, 8, 2, "data,s,r")', 2, (0, 2, 0)),
            ('disk.read_byte(7)', 0, (0, 3, 0)),
            ('disk.write_byte(7, 255)', 0, (0, 3, 0)),
        ]:
            with self.subTest(source=source):
                p = self.compile(f'''import c64.disk as disk
result = {source}
error: u8 = disk.error()
status: u8 = disk.status()
failed: bool = disk.failed()
print("screen")
''')
                p.disk = DiskKernal()
                p.disk.open_error = setup
                p.run()
                self.assertEqual(tuple(p.value(n) for n in ('result', 'error', 'status')), expected)
                self.assertEqual(p.value('failed'), 1)
                self.assertEqual(p.output, b'SCREEN\r')
                self.assertIsNone(p.disk.input)
                self.assertIsNone(p.disk.output)

    def test_disk_flush_and_read_errors(self):
        for writing, status in [(True, 1), (False, 2), (False, 128)]:
            with self.subTest(writing=writing, status=status):
                action = 'disk.write_byte(2, 255)' if writing else 'disk.read_byte(2)'
                mode = 'w' if writing else 'r'
                p = self.compile(f'''import c64.disk as disk
disk.open(2, 8, 2, "data,s,{mode}")
result = {action}
status: u8 = disk.status()
error: u8 = disk.error()
failed: bool = disk.failed()
disk.close(2)
print("screen")
''')
                p.disk = DiskKernal()
                p.disk.files[b'DATA'] = bytearray([42])
                p.disk.flush_status = status if writing else 0
                p.disk.read_status = 0 if writing else status
                p.run()
                self.assertEqual(p.value('result'), 0 if writing else 42)
                self.assertEqual(p.value('status'), status)
                self.assertEqual(p.value('error'), 0)
                self.assertEqual(p.value('failed'), 1)
                self.assertEqual(p.output, b'SCREEN\r')

    def test_disk_arguments_and_reexports(self):
        p = self.compile('''from c64.disk import open, close, write_byte, error, EOF
def argument(value: u8) -> u8:
    close(value)
    return value
opened: bool = open(argument(2), argument(8), argument(4), "order,s,w")
written: bool = write_byte(argument(6) - 4, argument(255))
saved: u8 = error()
eof_bit: u8 = EOF
close(2)
''')
        p.disk = DiskKernal()
        p.run()
        self.assertEqual(p.disk.opened, [(2, 8, 4, b'ORDER,S,W')])
        self.assertEqual(p.disk.closed, [2, 8, 4, 6, 255, 2])
        self.assertEqual(p.disk.files[b'ORDER'], b'\xff')
        self.assertEqual([p.value(n) for n in ('opened', 'written', 'saved', 'eof_bit')],
                         [1, 1, 0, 64])

    def test_disk_filename_length_guards(self):
        for length in (0, 255):
            with self.subTest(length=length):
                p = self.compile('from c64.kernal import open\n' +
                                 f'opened: bool = open(2, 8, 2, "{"a" * length}")\n')
                p.disk = DiskKernal()
                p.run()
                self.assertEqual(len(p.disk.opened[0][3]), length)
                self.assertEqual(p.value('opened'), 1)
        self.reject('from c64.kernal import open\n' +
                    f'open(2, 8, 2, "{"a" * 256}")\n', 'at most 255 bytes')
        p = self.compile('from c64.kernal import open\n' +
                         f'name: str = "{"a" * 256}"\nopen(2, 8, 2, name)\n')
        p.disk = DiskKernal()
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        self.assertFalse(p.disk.opened)

    def test_disk_signature_diagnostics(self):
        for source, message in [
            ('open(2, 8, 2)', 'expects 4 arguments'),
            ('open(2, 8, 2, 42)', 'str'),
            ('open(256, 8, 2, "data")', 'u8'),
            ('write_byte(2, -1)', 'u8'),
            ('read_byte()', 'expects 1 arguments'),
            ('status(2)', 'expects 0 arguments'),
        ]:
            with self.subTest(source=source):
                self.reject('from c64.kernal import open, write_byte, read_byte, status\n' +
                            source + '\n', message)

    def test_disk_library_under_cpython(self):
        script = '''
import sys
import types
kernal = types.ModuleType('c64.kernal')
state = {'status': 0, 'error': 0}
kernal.status = lambda: state['status']
kernal.error = lambda: state['error']
for name in ('open', 'close', 'read_byte', 'write_byte', 'write_text'):
    setattr(kernal, name, lambda *args: None)
sys.modules['c64.kernal'] = kernal
import c64.disk as disk
assert disk.open is kernal.open
for status, error, eof, failed in [(0, 0, False, False), (64, 0, True, False),
                                  (65, 0, True, True), (128, 0, False, True),
                                  (0, 3, False, True)]:
    state.update(status=status, error=error)
    assert disk.eof() == eof
    assert disk.failed() == failed
'''
        result = subprocess.run([sys.executable, '-c', script], cwd=ROOT / 'stdlib',
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_disk_dos_library_under_cpython(self):
        script = '''
import sys
import types
kernal = types.ModuleType('c64.kernal')
state = {'status': 0, 'error': 0, 'record': b'', 'position': 0, 'write_ok': True}
def read_byte(logical):
    pos = state['position']
    state['position'] += 1
    if pos >= len(state['record']):
        state['status'] = 2
        return 0
    state['status'] = 64 if state['position'] == len(state['record']) else 0
    return state['record'][pos]
def write_text(logical, text):
    state['command'] = (logical, text)
    return state['write_ok']
kernal.read_byte = read_byte
kernal.write_text = write_text
kernal.status = lambda: state['status']
kernal.error = lambda: state['error']
for name in ('open', 'close', 'write_byte'):
    setattr(kernal, name, lambda *args: None)
sys.modules['c64.kernal'] = kernal
import c64.disk as disk
for code in range(100):
    record = f'{code:02},MESSAGE,12,34\\r'.encode()
    state.update(record=record, position=0, error=0)
    assert disk.dos_code(15) == code
    assert state['position'] == len(record)
for record in (b'X0,OK,00,00\\r', b'00,,00,00\\r', b'00,OK,00,00', b'X' * 80):
    state.update(record=record, position=0)
    assert disk.dos_code(15) == disk.DOS_UNAVAILABLE
    assert state['position'] == min(len(record), 64)
state.update(record=b'00,OK,00,00\\r', position=0)
assert disk.command(15, 'r0:new=old') == disk.DOS_OK
assert state['command'] == (15, 'r0:new=old')
state.update(position=0, write_ok=False)
assert disk.command(15, 'r0:new=old') == disk.DOS_UNAVAILABLE
assert state['position'] == 0
'''
        result = subprocess.run([sys.executable, '-c', script], cwd=ROOT / 'stdlib',
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'build')
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)

    def invoke(self, text, command='build', options=(), bindings=True):
        source = self.dir / 'main.py'
        source.write_text(library_bindings(text) if bindings else text, encoding='ascii')
        output = self.dir / ('main.lst' if command == 'asm' else 'main.PRG')
        mapping = self.dir / 'main.map'
        result = subprocess.run([str(COMPILER), command, str(source), '-o', str(output),
                                 '--map', str(mapping), *options], text=True, capture_output=True)
        return result, output, mapping

    def module(self, name, text):
        path = self.dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(library_bindings(text), encoding='ascii')
        return path

    def compile(self, text, options=()):
        result, output, mapping = self.invoke(text, options=options)
        self.assertEqual(result.returncode, 0, result.stderr)
        symbols = {}
        for line in mapping.read_text().splitlines():
            if line.startswith('variable '):
                _, name, address, size, kind = line.split()
                symbols[name] = (int(address[1:], 16), int(size), kind)
        binary = output.read_bytes()
        self.assertEqual(binary[:2], b'\x01\x08')
        self.assertEqual(binary[2:14], bytes([11, 8, 10, 0, 158, 50, 48, 54, 49, 0, 0, 0]))
        return Program(binary, symbols)

    def reject(self, text, message, options=(), diagnostic='main.py'):
        existing = self.dir / 'main.PRG'
        previous = existing.read_bytes() if existing.exists() else None
        result, output, _ = self.invoke(text, options=options)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        if previous is None:
            self.assertFalse(output.exists(), 'failed compilation created a PRG')
        else:
            self.assertEqual(output.read_bytes(), previous, 'failed compilation changed the previous PRG')
        self.assertRegex(result.stderr, re.escape(diagnostic) + r':\d+:\d+: error:')

    def test_print_numeric_limits_and_boolean_values(self):
        p = self.compile('a: i16 = -32768\nb: i8 = -128\nc: i16 = -1\n'
                         'd: u8 = 0\ne: u8 = 1\nf: i8 = 127\ng: u8 = 255\n'
                         'h: u16 = 256\ni: i16 = 32767\nj: u16 = 65535\n'
                         'print(a, b, c, d, e, f, g, h, i, j)\n'
                         'print(True, False, bool(j), char(65))\nprint()\n'
                         'print("", "score:", j, "")\n')
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'-32768 -128 -1 0 1 127 255 256 32767 65535\r'
                                  b'TRUE FALSE TRUE 65\r\r SCORE: 65535 \r')
        self.assertEqual(p.cpu.sp, 0xF0)
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])

    def test_print_decimal_oracle(self):
        values = [0, 9, 10, 99, 100, 999, 1000, 9999, 10000, 60000]
        rng = random.Random(6502)
        values += [rng.randrange(65536) for _ in range(30)]
        p = self.compile('value: u16 = 0\n' + ''.join(
            f'value = {value}\nprint(value)\n' for value in values))
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output.decode('ascii'), ''.join(f'{value}\r' for value in values))

    def test_print_argument_evaluation_and_page_crossing(self):
        p = self.compile('def show(value: u16) -> u16:\n'
                         '    print("inner", value)\n    return value + 1\n'
                         'print("outer", show(1), show(2), show(65534))\n'
                         f'print("{"x" * 300}", 1001)\n')
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'INNER 1\rINNER 2\rINNER 65534\rOUTER 2 3 65535\r' +
                                  b'X' * 300 + b' 1001\r')
        p = self.compile('print(' + ', '.join(str(i) for i in range(16)) + ')\n')
        p.run()
        self.assertEqual(p.output.decode('ascii'), ' '.join(str(i) for i in range(16)) + '\r')

    def test_print_restrictions(self):
        self.reject('print(' + ', '.join('1' for _ in range(17)) + ')\n', 'at most 16 arguments')
        self.reject('values = array(u8, 3)\nprint(values)\n', 'print supports strings, integers and booleans')
        self.reject('print(poke(4096, 1))\n', 'print supports strings, integers and booleans')
        self.reject('print("hello", end="")\n', 'keyword arguments are not supported')

    def test_keyboard_buffer(self):
        p = self.compile('import c64.keyboard as keyboard\n'
                         'first = keyboard.read()\nsecond = keyboard.read()\n'
                         'keyboard.clear()\nempty = keyboard.read()\ncount = peek(198)\n')
        p.cpu.memory[0xC6] = 3
        p.cpu.memory[0x0277:0x027A] = [65, 66, 67]
        p.run()
        self.assertEqual([p.value(s) for s in ['first', 'second', 'empty', 'count']], [65, 66, 0, 0])
        p = self.compile('from c64.keyboard import read as get_key\nkey = get_key()\n')
        p.run()
        self.assertEqual(p.value('key'), 0)
        p = self.compile((ROOT / 'tests/fixtures/keyboard.py').read_text())
        p.run()
        self.assertEqual(p.output, b'KEYS: 65 66 0\r')

    def test_input_prompt_editing_empty_and_string_operations(self):
        p = self.compile('''first = input("Name? ")
empty = input()
last = input("Again? ")
print(first, len(first), first[-1], empty == "", last)
''')
        p.keyboard = [20, 0, 65, 66, 20, 67, 13, 13, 88, 13]
        p.clobber_kernal = True
        p.run()
        self.assertEqual(p.output, b'NAME? AB\x14C\r\rAGAIN? X\rAC 2 C TRUE X\r')
        for name, text in [('first', b'AC'), ('empty', b''), ('last', b'X')]:
            pointer = p.value(name)
            self.assertEqual(bytes(p.cpu.memory[pointer:pointer + len(text) + 1]), text + b'\0')
            self.assertEqual(p.cpu.memory[pointer - 2:pointer], [len(text), 0])
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
        self.assertEqual(p.cpu.sp, 0xF0)

    def test_input_repeated_function_calls_keep_immutable_values(self):
        p = self.compile('''def ask(prompt):
    return input(prompt)
first = ask("First? ")
second = ask("Second? ")
words = [first, second]
for word in words:
    print(word)
print(first, second, len(first), len(second))
''')
        for _ in range(2):
            p.keyboard = list(b'ONE\rTWO\r')
            p.output.clear()
            p.run()
            self.assertEqual(p.output, b'FIRST? ONE\rSECOND? TWO\rONE\rTWO\rONE TWO 3 3\r')
            self.assertNotEqual(p.value('first'), p.value('second'))
            p.restart()
        p = self.compile('print(input(input()))\n')
        p.keyboard = list(b'PROMPT\rANSWER\r')
        p.run()
        self.assertEqual(p.output, b'PROMPT\rPROMPTANSWER\rANSWER\r')

    def test_input_case_translation_and_control_keys(self):
        for charset, keys, expected in [(0x14, [65, 193, 66], b'AB'),
                                        (0x16, [65, 193, 90, 218], b'aAzZ')]:
            with self.subTest(charset=charset):
                p = self.compile('text = input()\nlength = len(text)\n')
                p.cpu.memory[0xD018] = charset
                p.keyboard = [0, 17, 147, 133, *keys, 13]
                p.run()
                pointer = p.value('text')
                self.assertEqual(bytes(p.cpu.memory[pointer:pointer + len(expected)]), expected)
                self.assertEqual(p.value('length'), len(expected))

    def test_input_line_length_and_pool_bounds(self):
        p = self.compile('text = input()\nlength = len(text)\nlast = text[-1]\n')
        p.keyboard = [65] * 255 + [13]
        p.run()
        self.assertEqual(p.value('length'), 255)
        self.assertEqual(p.cpu.memory[p.value('last')], 65)
        p = self.compile('text = input()\n')
        p.keyboard = [65] * 256 + [13]
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        self.assertEqual(p.output, b'A' * 255)
        source = '''saved = input()
completed: u8 = 0
for index in range(15):
    text = input()
    completed += 1
extra = input()
'''
        p = self.compile(source)
        # 15 full lines + a 223-byte line fill all 4096 bytes exactly.
        p.keyboard = (list(b'A' * 255 + b'\r') * 15 + list(b'B' * 223 + b'\r\r'))
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        self.assertEqual(p.value('completed'), 15)
        self.assertEqual(bytes(p.cpu.memory[p.value('saved'):p.value('saved') + 255]), b'A' * 255)
        self.assertEqual(bytes(p.cpu.memory[p.value('text'):p.value('text') + 223]), b'B' * 223)

    def test_input_diagnostics_pruning_and_stack_budget(self):
        self.reject('input("one", "two")\n', 'zero or one string prompt')
        self.reject('input(123)\n', 'cannot assign')
        self.reject('input = 1\n', 'reserved compiler builtin')
        result, _, mapping = self.invoke('def unused() -> str:\n    return input()\nprint("ready")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('runtime input_strings ', mapping.read_text())
        definitions = 'def f0() -> str:\n    return input()\n'
        definitions += ''.join(f'def f{i}() -> str:\n    return f{i-1}()\n' for i in range(1, 64))
        self.reject(definitions + 'print(f63())\n', 'stack')

    def test_input_prompt_and_return_match_cpython(self):
        source = '''def prompt():
    print("preparing")
    return "Answer: "
answer = input(prompt())
other = input()
print(answer, other, answer == "YES", len(answer))
'''
        result = subprocess.run([sys.executable, '-c', source], input='YES\nNO\n',
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        p = self.compile(source)
        p.keyboard = list(b'YES\rNO\r')
        p.run()
        # The C64 editor echoes typed keys; piped CPython input doesn't.
        self.assertEqual(p.output, b'PREPARING\rANSWER: YES\rNO\rYES NO TRUE 3\r')
        self.assertEqual(result.stdout, 'preparing\nAnswer: YES NO True 3\n')


    def test_sprite_registers_all_slots(self):
        for slot in range(8):
            p = self.compile('import c64.sprite as sprite\n'
                             f'slot: u8 = {slot}\nx: u16 = 511\ny: u8 = 255\n'
                             'sprite.enable(slot)\nsprite.position(slot, x, y)\n'
                             'sprite.color(slot, 15)\nsprite.data(slot, 16320)\n')
            p.cpu.memory[0xD000:0xD010] = [34] * 16
            p.cpu.memory[0xD015] = p.cpu.memory[0xD010] = 0x55
            p.run()
            self.assertEqual(p.cpu.memory[0xD015], 0x55 | (1 << slot))
            self.assertEqual(p.cpu.memory[0xD010], 0x55 | (1 << slot))
            self.assertEqual(p.cpu.memory[0xD000 + 2 * slot:0xD002 + 2 * slot], [255, 255])
            self.assertEqual(p.cpu.memory[0xD027 + slot], 15)
            self.assertEqual(p.cpu.memory[0x07F8 + slot], 255)
            for other in range(8):
                if other != slot:
                    self.assertEqual(p.cpu.memory[0xD000 + 2 * other], 34)
            p = self.compile('from c64.sprite import disable, position\n'
                             f'disable({slot})\nposition({slot}, 0, 0)\n')
            p.cpu.memory[0xD015] = p.cpu.memory[0xD010] = 255
            p.run()
            self.assertEqual(p.cpu.memory[0xD015], 255 ^ (1 << slot))
            self.assertEqual(p.cpu.memory[0xD010], 255 ^ (1 << slot))

    def test_sprite_argument_snapshots(self):
        p = self.compile('import c64.sprite as sprite\n'
                         'def nested(value: u16) -> u16:\n'
                         '    sprite.position(7, 500, 80)\n    print(value)\n    return value\n'
                         'sprite.position(1, nested(300), u8(nested(50)))\n')
        p.run()
        self.assertEqual(p.cpu.memory[0xD002:0xD004], [44, 50])
        self.assertEqual(p.cpu.memory[0xD00E:0xD010], [244, 80])
        self.assertEqual(p.cpu.memory[0xD010], 130)

    def test_sprite_objects_all_slots_and_shared_bit_preservation(self):
        for slot in range(8):
            with self.subTest(slot=slot):
                p = self.compile(f'''from c64.sprite import Sprite, shared_colors
p = Sprite({slot})
p.x = 511
p.y = 255
p.color = 15
p.set_data(16320)
p.enabled = True
p.multicolor = True
p.expand_x = True
p.expand_y = True
shared_colors(2, 5)
print(p.index, p.x, p.y, p.color, p.enabled, p.multicolor, p.expand_x, p.expand_y)
p.x = 0
p.y = 0
p.disable()
p.multicolor = False
p.expand_x = False
p.expand_y = False
''')
                p.cpu.memory[0xD000:0xD030] = [0x55] * 48
                p.clobber_kernal = True
                p.run()
                self.assertEqual(p.output, f'{slot} 511 255 15 TRUE TRUE TRUE TRUE\r'.encode())
                self.assertEqual(p.symbols['p'][1:], (1, 'object'))
                for address in (0xD010, 0xD015, 0xD01C, 0xD01D, 0xD017):
                    self.assertEqual(p.cpu.memory[address], 0x55 & ~(1 << slot))
                self.assertEqual(p.cpu.memory[0xD000 + 2 * slot:0xD002 + 2 * slot], [0, 0])
                self.assertEqual(p.cpu.memory[0xD027 + slot], 15)
                self.assertEqual(p.cpu.memory[0x07F8 + slot], 255)
                self.assertEqual(p.cpu.memory[0xD025:0xD027], [2, 5])
                for other in range(8):
                    if other != slot:
                        self.assertEqual(p.cpu.memory[0xD000 + 2 * other:0xD002 + 2 * other], [0x55, 0x55])
                        self.assertEqual(p.cpu.memory[0xD027 + other], 0x55)

    def test_sprite_objects_constructor_and_raw_api_interoperation(self):
        p = self.compile('''from c64 import sprite as sprites
first = sprites.Sprite(3)
second = sprites.Sprite(3)
before_x = first.x
before_y = first.y
before_color = first.color
sprites.position(3, 400, 90)
sprites.color(3, 6)
sprites.enable(3)
print(second.x, second.y, second.color, second.enabled)
first.x -= 1
print(second.x, second.y)
second.enabled = False
print(first.enabled)
''')
        p.cpu.memory[0xD006:0xD008] = [44, 55]
        p.cpu.memory[0xD010] = 8
        p.cpu.memory[0xD02A] = 0xF3
        p.run()
        self.assertEqual([p.value(n) for n in ('before_x', 'before_y', 'before_color')], [300, 55, 3])
        self.assertEqual(p.output, b'400 90 6 TRUE\r399 90\rFALSE\r')
        self.module('exports.py', 'from c64.sprite import Sprite as Player\n')
        p = self.compile('from exports import Player\np = Player(0)\np.enable()\n')
        p.run()
        self.assertEqual(p.cpu.memory[0xD015], 1)

    def test_sprite_objects_invalid_inputs_trap_before_hardware_writes(self):
        for action in ('p.x = 512', 'p.color = 16', 'p.set_data(12289)',
                       'p.set_data(16384)', 'shared_colors(2, 16)', 'shared_colors(16, 2)'):
            with self.subTest(action=action):
                p = self.compile('from c64.sprite import Sprite, shared_colors\np = Sprite(0)\n' + action + '\n')
                p.cpu.memory[0xD000:0xD030] = [0x55] * 48
                p.cpu.memory[0x07F8:0x0800] = [0xAA] * 8
                with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                    p.run()
                self.assertEqual(p.cpu.memory[0xD000:0xD030], [0x55] * 48)
                self.assertEqual(p.cpu.memory[0x07F8:0x0800], [0xAA] * 8)
        p = self.compile('from c64.sprite import Sprite\np = Sprite(8)\n')
        with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
            p.run()
        self.reject('from c64.sprite import Sprite\np = Sprite(0)\np.index = 2\n', 'read-only')

    def test_sprite_objects_match_cpython_with_memory_adapter(self):
        source = '''from c64.sprite import Sprite, shared_colors, position
p = Sprite(7)
p.x = 350
p.y = 80
p.color = 3
p.set_data(12288)
p.enabled = True
p.multicolor = True
p.expand_x = True
p.expand_y = False
shared_colors(2, 5)
print(p.x, p.y, p.color, p.enabled, p.multicolor, p.expand_x, p.expand_y)
position(7, 100, 90)
print(p.x, p.y)
p.disable()
print(p.enabled)
'''
        script = '''import sys, types
sys.path.insert(0, "stdlib")
ram = [0] * 65536
ram[0xD000:0xD030] = [0x55] * 48
memory = types.ModuleType("c64.memory")
memory.peek = lambda address: ram[address]
memory.poke = lambda address, value: ram.__setitem__(address, value)
sys.modules["c64.memory"] = memory
exec(sys.argv[1])
from c64.sprite import Sprite, shared_colors
for action in [lambda: Sprite(-1), lambda: Sprite(8), lambda: shared_colors(-1, 2),
               lambda: shared_colors(2, 16)]:
    before = ram[:]
    try:
        action()
    except AssertionError:
        assert ram == before
    else:
        raise AssertionError("invalid sprite argument accepted")
print(bytes(ram[0xD000:0xD030]).hex())
print(bytes(ram[0x07F8:0x0800]).hex())
'''
        result = subprocess.run([sys.executable, '-c', script, source], cwd=ROOT,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        p = self.compile(source)
        p.cpu.memory[0xD000:0xD030] = [0x55] * 48
        p.run()
        self.assertEqual(p.output, '\r'.join(lines[:-2]).upper().encode() + b'\r')
        self.assertEqual(bytes(p.cpu.memory[0xD000:0xD030]), bytes.fromhex(lines[-2]))
        self.assertEqual(bytes(p.cpu.memory[0x07F8:0x0800]), bytes.fromhex(lines[-1]))

    def test_sprite_objects_example_restart_and_pruning(self):
        p = self.compile((ROOT / 'examples/sprite_objects/main.py').read_text())
        for _ in range(2):
            p.output.clear()
            p.run()
            self.assertEqual(p.output, b'SPRITE: 299 80 1 TRUE\rMODES: TRUE TRUE FALSE\r')
            self.assertEqual([p.value(n) for n in ('x', 'y', 'color', 'pointer')], [299, 80, 1, 192])
            self.assertEqual(p.cpu.memory[0x3000:0x3040], [255] * 64)
            p.restart()
        result, _, mapping = self.invoke('from c64.sprite import Sprite\np = Sprite(0)\np.enable()\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('function c64.sprite.Sprite.x.', mapping.read_text())
        self.assertNotIn('function c64.sprite._flag ', mapping.read_text())

    def test_sprite_static_and_runtime_guards(self):
        prefix = 'import c64.sprite as sprite\n'
        for call, message in [('enable(8)', 'sprite index must be 0..7'),
                              ('position(0, 512, 1)', 'sprite X coordinate must be 0..511'),
                              ('color(0, 16)', 'sprite color must be 0..15'),
                              ('data(0, 12289)', '64-byte aligned'),
                              ('data(0, 16384)', 'VIC bank 0')]:
            self.reject(prefix + f'sprite.{call}\n', message)
        for declaration, call in [('index: u8 = 8', 'enable(index)'),
                                   ('x: u16 = 512', 'position(0, x, 1)'),
                                   ('color: u8 = 16', 'color(0, color)'),
                                   ('address: u16 = 12289', 'data(0, address)'),
                                   ('address: u16 = 16384', 'data(0, address)')]:
            p = self.compile(prefix + declaration + '\n' + f'sprite.{call}\n')
            with self.assertRaisesRegex(RuntimeError, 'runtime BRK guard'):
                p.run()

    def test_sid_voice_registers_and_shadows(self):
        for voice in range(3):
            p = self.compile('import c64.sid as sid\n'
                             f'voice: u8 = {voice}\n'
                             'freq_ok = sid.frequency(voice, 65535)\n'
                             'pulse_ok = sid.pulse_width(voice, 4095)\n'
                             'adsr_ok = sid.envelope(voice, 2, 4, 12, 5)\n'
                             'ctrl_ok = sid.control(voice, sid.PULSE | sid.SYNC)\n'
                             'on_ok = sid.gate(voice, True)\n'
                             'off_ok = sid.gate(voice, False)\n')
            device = SIDMemory(p.cpu.memory)
            p.cpu.memory = device
            p.run()
            base = 0xD400 + voice * 7
            self.assertEqual([device.registers[base + offset] for offset in range(7)],
                             [255, 255, 255, 15, 66, 36, 197])
            self.assertEqual([value for address, value in device.writes if address == base + 4],
                             [66, 67, 66])
            self.assertEqual(set(device.registers), set(range(base, base + 7)))
            self.assertEqual(device.reads, [])
            self.assertTrue(all(p.value(name) for name in ['freq_ok', 'pulse_ok', 'adsr_ok',
                                                         'ctrl_ok', 'on_ok', 'off_ok']))

    def test_sid_filter_and_volume_preserve_fields(self):
        p = self.compile('import c64.sid as sid\n'
                         'a = sid.volume(10)\n'
                         'b = sid.filter(2047, 15, 7, sid.LOW_PASS | sid.VOICE3_OFF)\n'
                         'c = sid.volume(3)\n'
                         'd = sid.filter(0, 0, 8, sid.BAND_PASS)\n'
                         'e = sid.volume(15)\n')
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual([value for address, value in device.writes if address == 0xD418],
                         [10, 154, 147, 35, 47])
        self.assertEqual([device.registers[address] for address in range(0xD415, 0xD419)],
                         [0, 0, 8, 47])
        self.assertEqual(device.writes[1:4], [(0xD415, 7), (0xD416, 255), (0xD417, 247)])
        self.assertEqual(device.reads, [])
        self.assertTrue(all(p.value(name) for name in 'abcde'))

    def test_sid_rejects_invalid_inputs_without_writes(self):
        calls = ['frequency(3, 65535)', 'pulse_width(255, 0)', 'pulse_width(0, 4096)',
                 'control(3, 255)', 'gate(3, True)', 'envelope(3, 1, 2, 3, 4)',
                 'envelope(0, 16, 0, 0, 0)', 'envelope(0, 0, 16, 0, 0)',
                 'envelope(0, 0, 0, 16, 0)', 'envelope(0, 0, 0, 0, 16)',
                 'volume(16)', 'filter(2048, 0, 0, 0)', 'filter(0, 16, 0, 0)',
                 'filter(0, 0, 16, 0)', 'filter(0, 0, 0, 1)']
        source = 'import c64.sid as sid\n' + ''.join(
            f'bad{i} = sid.{call}\n' for i, call in enumerate(calls))
        p = self.compile(source)
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual(device.writes, [])
        self.assertFalse(any(p.value(f'bad{i}') for i in range(len(calls))))

    def test_sid_reset_clears_hardware_and_shadow_state(self):
        p = self.compile('import c64.sid as sid\n'
                         'sid.control(0, 255)\nsid.volume(15)\n'
                         'sid.filter(1000, 15, 15, sid.HIGH_PASS)\n'
                         'sid.reset()\nsid.gate(0, True)\nsid.volume(2)\n')
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual(device.writes[-27:-2], [(address, 0) for address in range(0xD400, 0xD419)])
        self.assertEqual(device.registers[0xD404], 1)
        self.assertEqual(device.registers[0xD418], 2)
        self.assertEqual(device.reads, [])
        p.restart()
        p.run()
        self.assertEqual(device.registers[0xD404], 1)
        self.assertEqual(device.registers[0xD418], 2)

    def test_audio_notes_and_reexports(self):
        p = self.compile('from c64.audio import note_on as play, note_off as stop, volume, TRIANGLE\n'
                         'volume(10)\na = play(1, 7493, TRIANGLE, 2, 4, 12, 5)\n'
                         'b = play(1, 1000, TRIANGLE, 0, 1, 2, 3)\nc = stop(1)\n')
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual([value for address, value in device.writes if address == 0xD40B],
                         [0, 17, 16, 17, 16])
        self.assertEqual([device.registers[0xD407 + i] for i in [0, 1, 4, 5, 6]],
                         [232, 3, 16, 1, 35])
        self.assertNotIn(0xD409, device.registers)
        self.assertNotIn(0xD40A, device.registers)
        self.assertEqual(device.registers[0xD418], 10)
        self.assertEqual(device.reads, [])
        self.assertEqual([p.value(name) for name in 'abc'], [1, 1, 1])
        p = self.compile('import c64.audio as audio\n'
                         'result = audio.note_on(2, 1000, audio.PULSE, 1, 2, 3, 4)\n')
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual(device.registers[0xD410], 0)
        self.assertEqual(device.registers[0xD411], 8)
        self.assertEqual(device.registers[0xD412], 65)

    def test_audio_invalid_notes_leave_registers_untouched(self):
        calls = ['note_on(3, 1000, 16, 1, 2, 3, 4)', 'note_on(0, 1000, 0, 1, 2, 3, 4)',
                 'note_on(0, 1000, 17, 1, 2, 3, 4)', 'note_on(0, 1000, 48, 1, 2, 3, 4)',
                 'note_on(0, 1000, 16, 16, 2, 3, 4)',
                 'note_on(0, 1000, 16, 1, 16, 3, 4)', 'note_on(0, 1000, 16, 1, 2, 16, 4)',
                 'note_on(0, 1000, 16, 1, 2, 3, 16)', 'note_off(3)']
        p = self.compile('import c64.audio as audio\n' + ''.join(
            f'bad{i} = audio.{call}\n' for i, call in enumerate(calls)))
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual(device.writes, [])
        self.assertFalse(any(p.value(f'bad{i}') for i in range(len(calls))))

    def test_sid_modules_imports_diagnostics_and_pruning(self):
        p = self.compile('import c64.sid\nresult = c64.sid.frequency(0, 1234)\n')
        p.run()
        self.assertEqual(p.value('result'), 1)
        self.module('sound_helpers.py', 'from c64.audio import note_on as start, TRIANGLE\n')
        p = self.compile('from sound_helpers import start, TRIANGLE\n'
                         'result = start(0, 7493, TRIANGLE, 2, 4, 12, 5)\n')
        p.run()
        self.assertEqual(p.value('result'), 1)
        self.reject('from c64.sid import missing\n', "has no export 'missing'")
        self.reject('import c64.sid as sid\nsid.volume(256)\n', 'does not fit in u8')
        result, _, mapping = self.invoke('import c64.sid as sid\nsid.frequency(0, 1)\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('function c64._sid.frequency ', mapping.read_text())
        self.assertNotIn('function c64._sid.filter ', mapping.read_text())
        self.assertNotIn('variable c64._sid.filter.', mapping.read_text())
        self.assertNotIn('function c64._sid_voice.Voice.', mapping.read_text())

    def test_sid_libraries_run_under_python_with_hardware_adapter(self):
        result = subprocess.run([sys.executable, '-c',
            'import sys, types; registers = {}; '
            'memory = types.ModuleType("c64.memory"); '
            'memory.poke = lambda address, value: registers.__setitem__(int(address), int(value)); '
            'memory.peek = lambda address: registers.get(int(address), 0); '
            'sys.modules["c64.memory"] = memory; '
            'from c64 import sid, audio; '
            'audio.reset(); assert audio.volume(10); '
            'assert audio.note_on(0, 7493, audio.TRIANGLE, 2, 4, 12, 5); '
            'assert [registers[0xD400+i] for i in range(7)] == [69, 29, 0, 0, 17, 36, 197]; '
            'assert audio.note_off(0) and registers[0xD404] == 16; '
            'assert sid.filter(1023, 8, 1, sid.LOW_PASS) and registers[0xD418] == 26; '
            'before = registers.copy(); assert not audio.note_on(3, 1, 16, 2, 4, 12, 5); '
            'assert registers == before'],
            cwd=ROOT / 'stdlib', text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_module_namespace_reexports(self):
        self.module('devices/numbers.py', 'value = 7\ndef add(x: u8) -> u8:\n    return x + value\n')
        self.module('ports.py', 'import devices.numbers as port\n')
        self.module('bridge.py', 'from ports import port as device\n')
        p = self.compile('from bridge import device as number\nresult = number.add(3)\nprint(number.value, result)\n')
        p.run()
        self.assertEqual(p.output, b'7 10\r')
        self.reject('from bridge import device\nprint(device)\n', 'module namespace requires a member')
        self.reject('from bridge import missing\n', 'has no export')

    def test_sid_voice_handles_properties_and_shared_shadows(self):
        for index in range(3):
            source = f'''from c64.audio import sid
sid.reset()
first = sid.voice({index})
second = sid.Voice({index})
first.frequency_word = 12345
first.pulse_width = 2048
first.set_envelope(2, 4, 12, 5)
first.control = sid.PULSE | sid.SYNC
first.gate = True
word = second.frequency_word
width = second.pulse_width
attack = second.attack
decay = second.decay
sustain = second.sustain
release = second.release
enabled = second.gate
sid.frequency({index}, 54321)
updated = first.frequency_word
sid.envelope({index}, 1, 2, 3, 4)
updated_attack = second.attack
updated_release = second.release
second.disable()
first.enable()
second.sync = False
bits = first.control
'''
            p = self.compile(source)
            device = SIDMemory(p.cpu.memory)
            p.cpu.memory = device
            p.run()
            self.assertEqual([p.value(name) for name in ['word', 'width', 'attack', 'decay', 'sustain', 'release', 'enabled']],
                             [12345, 2048, 2, 4, 12, 5, 1])
            self.assertEqual([p.value(name) for name in ['updated', 'updated_attack', 'updated_release', 'bits']],
                             [54321, 1, 4, 65])
            self.assertEqual(p.symbols['first'][1], 1)
            self.assertEqual(p.symbols['second'][1], 1)
            base = 0xD400 + index * 7
            self.assertEqual([device.registers[base + offset] for offset in range(7)], [49, 212, 0, 8, 65, 18, 52])
            self.assertEqual(device.reads, [])
            self.assertTrue(all(value == 0 for address, value in device.registers.items()
                                if not base <= address < base + 7))

    def test_sid_voice_flags_envelopes_and_reset(self):
        p = self.compile('''from c64 import sid
sid.reset()
voice = sid.voice(2)
voice.control = 255
voice.gate = False
voice.sync = False
voice.ring_mod = False
voice.test = False
flags_off = voice.control
voice.waveform = sid.TRIANGLE
voice.gate = True
voice.sync = True
voice.ring_mod = True
voice.test = True
flags_on = voice.control
voice.set_envelope(1, 2, 3, 4)
voice.attack = 5
voice.decay = 6
voice.sustain = 7
voice.release = 8
voice.frequency_word = 30000
voice.frequency_word += 1
word = voice.frequency_word
sid.reset()
cleared = voice.control
cleared_word = voice.frequency_word
cleared_width = voice.pulse_width
cleared_attack = voice.attack
voice.gate = True
''')
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual([p.value(name) for name in ['flags_off', 'flags_on', 'word']], [240, 31, 30001])
        self.assertEqual([p.value(name) for name in ['cleared', 'cleared_word', 'cleared_width', 'cleared_attack']], [0, 0, 0, 0])
        self.assertEqual([value for address, value in device.writes if address == 0xD412],
                         [0, 255, 254, 252, 248, 240, 16, 17, 19, 23, 31, 0, 1])
        self.assertIn((0xD413, 86), device.writes)
        self.assertIn((0xD414, 120), device.writes)
        self.assertEqual(device.reads, [])

    def test_sid_voice_guards_and_unused_pruning(self):
        for operation in ['voice.pulse_width = 4096', 'voice.attack = 16',
                          'voice.decay = 16', 'voice.sustain = 16', 'voice.release = 16',
                          'voice.waveform = 17', 'voice.set_envelope(0, 0, 0, 16)']:
            p = self.compile('from c64.sid import Voice\nvoice = Voice(0)\n' + operation + '\n')
            device = SIDMemory(p.cpu.memory)
            p.cpu.memory = device
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()
            self.assertEqual(device.writes, [])
        for index in [3, 255]:
            p = self.compile(f'from c64.sid import voice\nhandle = voice({index})\n')
            device = SIDMemory(p.cpu.memory)
            p.cpu.memory = device
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()
            self.assertEqual(device.writes, [])
        self.reject('from c64.sid import Voice\np = Voice(0)\np.index = 1\n', 'read-only')
        result, _, mapping = self.invoke('from c64.sid import voice\np = voice(0)\nindex = p.index\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('function c64._sid_voice.Voice.__init__ ', mapping.read_text())
        self.assertNotIn('function c64._sid_voice.Voice.frequency.', mapping.read_text())
        self.assertNotIn('function c64._sid.hz_to_word ', mapping.read_text())

    def test_sid_voice_nominal_references_modules_and_sprite_interop(self):
        self.module('instruments.py', 'from c64.sid import Voice as Instrument, voice\n')
        p = self.compile('''from instruments import Instrument, voice
from c64 import sprite, audio
def tune(sound: Instrument) -> u16:
    sound.frequency_word += 1
    return sound.frequency_word
lead = voice(0)
lead.frequency_word = 1000
player = sprite.Sprite(0)
player.x = 300
player.y = 80
result = tune(lead)
audio.volume(10)
''')
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual(p.value('result'), 1001)
        self.assertEqual(p.cpu.memory[0xD000:0xD003], [44, 80, 0])
        self.assertEqual(p.cpu.memory[0xD010] & 1, 1)
        self.assertEqual(device.registers[0xD400], 233)
        self.assertEqual(device.reads, [])

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for SID Hz execution checks')
    def test_sid_hz_pal_ntsc_auto_and_custom_clocks(self):
        for mode, flag, clock in [('sid.PAL', 0, 985248), ('sid.NTSC', 1, 1022730),
                                 ('sid.AUTO', 1, 985248), ('sid.AUTO', 0, 1022730)]:
            p = self.float_compile(f'''from c64.audio import sid
sid.set_clock({mode})
clock = sid.clock_hz()
zero = sid.hz_to_word(0)
word = sid.hz_to_word(440)
actual = sid.word_to_hz(word)
maximum = sid.word_to_hz(65535)
last = sid.hz_to_word(maximum)
voice = sid.voice(0)
voice.frequency = 440
stored = voice.frequency_word
frequency = voice.frequency
''')
            p.cpu.memory[0x02A6] = flag
            device = SIDMemory(p.cpu.memory)
            p.cpu.memory = device
            p.run()
            wanted = int(440 * 16777216 / clock + 0.5)
            self.assertEqual(p.value('clock'), clock)
            self.assertEqual(p.value('word'), wanted)
            self.assertEqual(p.value('stored'), wanted)
            self.assertEqual(p.value('zero'), 0)
            self.assertEqual(p.value('last'), 65535)
            self.assertTrue(math.isclose(p.value('actual'), wanted * clock / 16777216, rel_tol=1e-8))
            self.assertTrue(math.isclose(p.value('frequency'), wanted * clock / 16777216, rel_tol=1e-8))
            self.assertEqual(device.reads, [])
        p = self.float_compile('''from c64 import sid
accepted = sid.set_clock_hz(1000000.0)
word = sid.hz_to_word(440)
sid.reset()
clock = sid.clock_hz()
bad = sid.set_clock_hz(0)
after = sid.clock_hz()
''')
        p.run()
        self.assertEqual(p.value('word'), int(440 * 16777216 / 1000000 + 0.5))
        self.assertEqual(p.value('clock'), 1000000)
        self.assertEqual(p.value('after'), 1000000)
        self.assertEqual(p.value('accepted'), 1)
        self.assertEqual(p.value('bad'), 0)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for SID Hz execution checks')
    def test_sid_hz_rejection_and_audio_preflight(self):
        p = self.float_compile('''from c64.audio import sid, note_on_hz
bad_mode = sid.set_clock(255)
sid.set_clock(sid.PAL)
bad_clock = sid.set_clock_hz(1e30)
negative = sid.frequency_hz(0, -1)
too_high = sid.frequency_hz(0, 1e38)
bad_voice = sid.frequency_hz(3, 440)
bad_note = note_on_hz(0, 1e38, sid.TRIANGLE, 1, 2, 3, 4)
bad_wave = note_on_hz(0, 440, 17, 1, 2, 3, 4)
bad_envelope = note_on_hz(0, 440, sid.TRIANGLE, 16, 2, 3, 4)
''')
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual(device.writes, [])
        for name in ['bad_mode', 'bad_clock', 'negative', 'too_high', 'bad_voice', 'bad_note', 'bad_wave', 'bad_envelope']:
            self.assertEqual(p.value(name), 0)
        for operation in ['sid.hz_to_word(-1)', 'sid.hz_to_word(1e38)', 'voice.frequency = -1', 'voice.frequency = 1e38']:
            p = self.float_compile('from c64 import sid\nvoice = sid.voice(0)\n' + operation + '\n')
            device = SIDMemory(p.cpu.memory)
            p.cpu.memory = device
            with self.assertRaisesRegex(RuntimeError, 'BRK guard'):
                p.run()
            self.assertEqual(device.writes, [])
        p = self.float_compile('''from c64.audio import note_on_hz, sid
sid.set_clock(sid.PAL)
result = note_on_hz(1, 440, sid.PULSE, 2, 4, 12, 5)
voice = sid.voice(1)
word = voice.frequency_word
enabled = voice.gate
''')
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        p.run()
        self.assertEqual(p.value('result'), 1)
        self.assertEqual(p.value('word'), 7493)
        self.assertEqual(p.value('enabled'), 1)
        self.assertEqual(device.registers[0xD40B], 65)

    def test_sid_voice_host_hardware_adapter_and_invalid_ranges(self):
        script = '''import types, sys
registers = {}
reads = []
memory = types.ModuleType("c64.memory")
def peek(address):
    assert not 0xD400 <= address <= 0xD418
    reads.append(address)
    return 1 if address == 0x02A6 else 0
memory.peek = peek
memory.poke = lambda address, value: registers.__setitem__(int(address), int(value))
sys.modules["c64.memory"] = memory
from c64.audio import sid, note_on_hz
assert sid.voice is sid.Voice
sid.reset()
first = sid.voice(0)
other = sid.Voice(0)
first.frequency = 440
assert other.frequency_word == 7493
assert abs(other.frequency - 7493 * 985248 / 16777216) < 1e-9
first.set_envelope(1, 2, 3, 4)
other.attack = 5
assert first.attack == 5 and first.decay == 2
first.control = sid.PULSE | sid.SYNC
first.gate = True
assert other.control == 67
before = registers.copy()
for call in [lambda: sid.frequency(-1, 1), lambda: sid.frequency(0, -1),
             lambda: sid.pulse_width(0, -1), lambda: sid.control(0, 256),
             lambda: sid.volume(-1), lambda: sid.filter(-1, 0, 0, 0),
             lambda: note_on_hz(0, float("nan"), 16, 1, 2, 3, 4)]:
    assert call() is False
    assert registers == before
for field, value in [("pulse_width", 4096), ("attack", 16), ("frequency", -1),
                     ("frequency", float("inf")), ("waveform", 17)]:
    try:
        setattr(first, field, value)
    except AssertionError:
        pass
    else:
        raise AssertionError(field)
    assert registers == before
sid.reset()
assert other.frequency_word == 0 and other.control == 0 and other.attack == 0
assert reads and set(reads) == {0x02A6}
'''
        result = subprocess.run([sys.executable, '-c', script], cwd=ROOT / 'stdlib', text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(os.environ.get('PYC64_BASIC_ROM'), 'configure a C64 BASIC ROM for SID voice demo checks')
    def test_sid_voice_example_native_rom_and_reentry(self):
        p = self.float_compile((ROOT / 'examples/sid_voices/main.py').read_text())
        device = SIDMemory(p.cpu.memory)
        p.cpu.memory = device
        before = device[2:0x90]
        for _ in range(2):
            p.output.clear()
            p.clobber_kernal = True
            p.run()
            self.assertEqual(p.output, b'SID VOICES: 7493 3746\rGATES: TRUE TRUE\r')
            self.assertEqual(bytes(device.registers[address] for address in range(0xD400, 0xD419)),
                             bytes.fromhex('45 1d 00 00 11 24 c5 a2 0e 00 08 43 12 34 00 00 00 00 00 00 00 00 00 00 0a'))
            self.assertEqual(device[2:0x90], before)
            self.assertEqual(device[0x300:0x302], [0x3A, 0xA4])
            self.assertEqual(device.reads, [])
            p.restart()

    def test_types_wrap_and_zero_page_restoration(self):
        p = self.compile('a: u8 = 255\na += 1\nb: i8 = -128\nb -= 1\n'
                         'c: u16 = 65535\nc = c + 1\nd: i16 = -32768\nd = d - 1\n')
        p.run()
        self.assertEqual([p.value(s) for s in 'abcd'], [0, 127, 0, 32767])
        self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])
        self.assertEqual(p.cpu.sp, 0xF0)
        p.restart()
        p.run()
        self.assertEqual(p.value('a'), 0)

    def test_arithmetic_oracle(self):
        rng = random.Random(64)
        for _ in range(18):
            a, b = rng.randrange(65536), rng.randrange(1, 65536)
            p = self.compile(f'a: u16 = {a}\nb: u16 = {b}\n'
                             'add: u16 = a + b\nsub: u16 = a - b\nmul: u16 = a * b\n'
                             'div: u16 = a // b\nmod: u16 = a % b\n'
                             'bits: u16 = (a & b) | (a ^ b)\n')
            p.run()
            self.assertEqual([p.value(s) for s in ['add', 'sub', 'mul', 'div', 'mod', 'bits']],
                             [(a+b)&65535, (a-b)&65535, (a*b)&65535, a//b, a%b, a|b])

    def test_signed_division_and_modulo_python_oracle(self):
        rng = random.Random(6510)
        for kind, bits in [('i8', 8), ('i16', 16)]:
            minimum, maximum = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
            pairs = [(minimum, -1), (minimum, 1), (minimum, minimum), (maximum, minimum),
                     (-7, 3), (7, -3), (-7, -3), (-6, 3), (6, -3), (0, -1),
                     (-1, maximum), (1, minimum), (maximum, -1), (minimum + 1, maximum)]
            pairs += [(rng.randint(minimum, maximum), rng.choice(
                [rng.randint(minimum, -1), rng.randint(1, maximum)])) for _ in range(35)]
            for a, b in pairs:
                with self.subTest(kind=kind, a=a, b=b):
                    p = self.compile(f'a: {kind} = {a}\nb: {kind} = {b}\nq = a // b\nr = a % b\n')
                    p.run()
                    quotient = ((a // b) - minimum) % (1 << bits) + minimum
                    self.assertEqual(p.value('q'), quotient)
                    self.assertEqual(p.value('r'), a % b)
                    self.assertEqual(p.cpu.sp, 0xF0)
                    self.assertEqual(p.cpu.memory[0xFB:0xFF], [11, 22, 33, 44])

    def test_signed_division_nested_operands_and_zero_guards(self):
        p = self.compile('a: i16 = -100\nb: i16 = 7\nc: i16 = 20\nd: i16 = -3\n'
                         'nested = (a // b) // (c // d)\nremainder = (a % b) % (c % d)\n'
                         'small: u8 = 255\nmixed = a // small\nmodulo = small % b\n')
        p.run()
        self.assertEqual([p.value(name) for name in ['nested', 'remainder', 'mixed', 'modulo']],
                         [(-100 // 7) // (20 // -3), (-100 % 7) % (20 % -3), -100 // 255, 255 % 7])
        for kind in ['i8', 'i16']:
            for op in ['//', '%']:
                p = self.compile(f'a: {kind} = -1\nb: {kind} = 0\nx = a {op} b\n')
                with self.assertRaisesRegex(RuntimeError, 'runtime BRK guard'):
                    p.run()

    def test_runtime_range_values_python_oracle(self):
        cases = [(0, 0, 1), (0, 5, 1), (2, 11, 3), (8, -3, -2),
                 (-8, 3, 2), (3, 8, -1), (8, 3, 1), (5, 5, -1),
                 (32760, 32767, 10), (-32760, -32768, -10),
                 (32767, -32768, -32768), (-32768, 32767, 32767),
                 (-32768, 32767, 10000)]
        for runtime in [False, True]:
            for start, stop, step in cases:
                with self.subTest(runtime=runtime, start=start, stop=stop, step=step):
                    arguments = f'{start}, {stop}, {step}'
                    source = ''
                    if runtime:
                        source = f'start: i16 = {start}\nstop: i16 = {stop}\nstep: i16 = {step}\n'
                        arguments = 'start, stop, step'
                    p = self.compile(source + 'i: i16 = 123\ncount: u16 = 0\n'
                                     f'for i in range({arguments}):\n    print(i)\n    count += 1\n')
                    p.run()
                    expected = list(range(start, stop, step))
                    self.assertEqual(p.output.decode('ascii'), ''.join(f'{value}\r' for value in expected))
                    self.assertEqual(p.value('count'), len(expected))
                    self.assertEqual(p.value('i'), expected[-1] if expected else 123)
        p = self.compile('stop: u16 = 5\nstart: u8 = 2\n'
                         'for i in range(stop):\n    print(i)\n'
                         'for j in range(start, stop):\n    print(j)\n')
        p.run()
        self.assertEqual(p.output, b'0\r1\r2\r3\r4\r2\r3\r4\r')

    def test_runtime_range_bounds_are_captured_once_in_order(self):
        p = self.compile('def bound(value: i16) -> i16:\n'
                         '    print("bound", value)\n    return value\n'
                         'for i in range(bound(1), bound(6), bound(2)):\n    print(i)\n'
                         'stop: i16 = 6\nstep: i16 = 2\n'
                         'for j in range(0, stop, step):\n'
                         '    print(j)\n    stop = 1\n    step = 0\n    j = 100\n')
        p.run()
        self.assertEqual(p.output, b'BOUND 1\rBOUND 6\rBOUND 2\r1\r3\r5\r0\r2\r4\r')
        self.assertEqual(p.value('j'), 100)
        p = self.compile('for i in range(peek(49152)):\n    print(i)\n    poke(49152, 0)\n')
        p.cpu.memory[49152] = 3
        p.run()
        self.assertEqual(p.output, b'0\r1\r2\r')

    def test_runtime_range_nested_loops_and_calls(self):
        p = self.compile('def bound(value: i16) -> i16:\n'
                         '    total: i16 = 0\n    for k in range(value):\n        total += 1\n'
                         '    return total\n'
                         'limit: i16 = 4\ncount: u16 = 0\n'
                         'for i in range(bound(limit)):\n'
                         '    if i == 1:\n        continue\n'
                         '    for j in range(bound(3)):\n'
                         '        if j == 2:\n            break\n        count += 1\n')
        p.run()
        self.assertEqual(p.value('count'), 6)
        self.assertIn('bound.total', p.symbols)
        self.reject('def f() -> i16:\n    for i in range(f()):\n        pass\n    return 1\n',
                    'recursion')
        chain = 'def f0() -> i16:\n    return 2\n' + ''.join(
            f'def f{i}() -> i16:\n    return f{i-1}()\n' for i in range(1, 64))
        self.reject(chain + 'for i in range(1, f63(), 1):\n    pass\n', 'stack')

    def test_runtime_range_type_and_argument_guards(self):
        for source in ['stop: u16 = 32768\nfor i in range(stop):\n    pass\n',
                       'start: u16 = 65535\nfor i in range(start, 2):\n    pass\n',
                       'step: u16 = 32768\nfor i in range(0, 2, step):\n    pass\n',
                       'step: i16 = 0\nfor i in range(0, 0, step):\n    pass\n']:
            p = self.compile(source)
            with self.assertRaisesRegex(RuntimeError, 'runtime BRK guard'):
                p.run()
        for kind, start, stop, step, previous in [('i8', 127, 129, 1, 127),
                                                  ('i8', -128, -130, -1, -128),
                                                  ('u8', 255, 257, 1, 255),
                                                  ('u8', -1, 0, 1, 0),
                                                  ('u16', -1, 0, 1, 0),
                                                  ('bool', 0, 3, 1, 1)]:
            source = f'i: {kind} = {"False" if kind == "bool" else "0"}\n'
            source += f'start: i16 = {start}\nstop: i16 = {stop}\nstep: i16 = {step}\n'
            p = self.compile(source + 'for i in range(start, stop, step):\n    pass\n')
            with self.assertRaisesRegex(RuntimeError, 'runtime BRK guard'):
                p.run()
            self.assertEqual(p.value('i'), previous)
        p = self.compile('i: u8 = 123\nstart: i16 = -1\nstop: i16 = -2\n'
                         'for i in range(start, stop):\n    pass\n'
                         'for i in range(-1, -2):\n    pass\n')
        p.run()
        self.assertEqual(p.value('i'), 123)
        self.reject('for i in range(32768):\n    pass\n', 'range arguments must fit in i16')
        self.reject('for i in range(u16(32768)):\n    pass\n', 'range arguments must fit in i16')
        self.reject('for i in range(0, 1, i8(0)):\n    pass\n', 'step cannot be zero')
        self.reject('for i in range("three"):\n    pass\n', 'range arguments must be integers')
        self.reject('i = "text"\nfor i in range(3):\n    pass\n', 'loop variable must be an integer')

    def test_signed_comparisons_and_shifts(self):
        for a, b in [(-32768, 32767), (-1, 0), (5, 5), (123, -9), (-200, -100)]:
            source = f'a: i16 = {a}\nb: i16 = {b}\n'
            for name, op in [('lt', '<'), ('le', '<='), ('gt', '>'), ('ge', '>='), ('eq', '=='), ('ne', '!=')]:
                source += f'{name}: bool = a {op} b\n'
            source += 'shift: i16 = a >> 3\n'
            p = self.compile(source)
            p.run()
            self.assertEqual([p.value(s) for s in ['lt', 'le', 'gt', 'ge', 'eq', 'ne', 'shift']],
                             [a<b, a<=b, a>b, a>=b, a==b, a!=b, a>>3])

    def test_control_flow(self):
        p = self.compile('count: u16 = 0\nfor i in range(256):\n    count += 1\n'
                         'for j in range(10, -1, -2):\n    if j == 6:\n        continue\n'
                         '    count += 2\n    if j == 2:\n        break\n'
                         'while count > 260:\n    count -= 1\n'
                         'if count < 260:\n    result = 0\nelif count == 260:\n    result = 1\nelse:\n    result = 2\n')
        p.run()
        self.assertEqual(p.value('count'), 260)
        self.assertEqual(p.value('result'), 1)

    def test_functions_arrays_and_nested_arguments(self):
        p = self.compile('def add(a: u16, b: u16) -> u16:\n    return a + b\n'
                         'def twice(a: u16) -> u16:\n    temporary: u16 = 0\n'
                         '    temporary += a\n    return temporary * 2\n'
                         'values: array[u16, 10] = array(u16, 10)\ntotal: u16 = 0\n'
                         'for i in range(10):\n    values[i] = twice(u16(i))\n'
                         '    total = add(total, values[i])\n'
                         'nested: u16 = add(add(2, 3), add(4, 5))\n'
                         'length: u16 = len(values)\n')
        p.run()
        self.assertEqual(p.value('total'), 90)
        self.assertEqual(p.value('nested'), 14)
        self.assertEqual(p.value('length'), 10)
        address = p.symbols['values'][0]
        self.assertEqual(bytes(p.cpu.memory[address:address+20]), b''.join((i*2).to_bytes(2,'little') for i in range(10)))

    def test_hardware_and_strings(self):
        p = self.compile('from c64.video import screen as display\nfrom c64.input import joystick\n'
                         'display.clear()\ndisplay.border = 6\ndisplay.background = 0\n'
                         'display.put_char(39, 24, "A")\n'
                         'message = "hello\\nworld"\nprint(message)\n'
                         'left: bool = joystick.port2.left\nright: bool = joystick.right\n'
                         'poke(49152, 42)\nread: u8 = peek(49152)\n')
        p.cpu.memory[0xDC00] = 0xFB
        p.run()
        self.assertEqual(p.cpu.memory[0xD020:0xD022], [6, 0])
        self.assertEqual(p.cpu.memory[0x0400:0x07E7], [32]*999)
        self.assertEqual(p.cpu.memory[0x07E7], 1)
        self.assertEqual(p.cpu.memory[0xD800:0xDBE8], [1]*1000)
        self.assertEqual(p.output, b'HELLO\rWORLD\r')
        self.assertEqual([p.value(s) for s in ['left', 'right', 'read']], [1, 0, 42])

    def test_short_circuit(self):
        p = self.compile('def effect() -> bool:\n    poke(49152, 1)\n    return True\n'
                         'zero: u8 = 0\none: u8 = 1\na: u8 = zero and effect()\n'
                         'b: u8 = one or effect()\n')
        p.run()
        self.assertEqual(p.cpu.memory[49152], 0)
        self.assertEqual([p.value(s) for s in ['a', 'b']], [0, 1])

    def test_initialization_and_casts(self):
        p = self.compile('empty: str = ""\nprint(empty)\n'
                         'a: array[u8, 257] = array(u8, 257)\na[256] = 42\n'
                         'x: i8 = i8(128)\ny: u16 = u16(-1)\n'
                         'z: bool = bool(y)\nt: bool = True\n'
                         'sum: u8 = t + t\nneg: i8 = -t\n')
        p.run()
        self.assertEqual(p.output, b'\r')
        self.assertEqual([p.value(s) for s in ['x', 'y', 'z', 'sum', 'neg']], [-128, 65535, 1, 2, -1])
        address = p.symbols['a'][0]
        self.assertEqual(p.cpu.memory[address:address+257], [0]*256+[42])

    def test_source_protection_and_existing_output(self):
        source = self.dir / 'main.py'
        source.write_text('x = 5\n')
        alias = str(self.dir) + '/./main.py'
        result = subprocess.run([str(COMPILER), str(source), '-o', alias], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(source.read_text(), 'x = 5\n')
        output = self.dir / 'main.PRG'
        output.write_bytes(b'previous build')
        result, _, _ = self.invoke('x: u8 = 300\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), b'previous build')

    def test_stack_budget(self):
        # Declaration order must not change acceptance of an acyclic call chain.
        for reverse in [False, True]:
            definitions = ['def f0() -> u8:\n    return 7\n']
            definitions += [f'def f{i}() -> u8:\n    return f{i-1}()\n' for i in range(1, 20)]
            if reverse:
                definitions.reverse()
            p = self.compile(''.join(definitions) + 'result = f19()\n')
            p.run()
            self.assertEqual(p.value('result'), 7)
        definitions = ['def f0() -> u8:\n    return 7\n']
        definitions += [f'def f{i}() -> u8:\n    return f{i-1}()\n' for i in range(1, 66)]
        self.reject(''.join(definitions) + 'result = f65()\n', 'stack')

    def test_runtime_guards(self):
        for source in ['a: u16 = 1\nb: u16 = 0\nx = a // b\n',
                       'a: array[u8, 2] = array(u8, 2)\ni: u8 = 2\nx = a[i]\n',
                       'a: u8 = 1\nb: u8 = 16\nx = a << b\n']:
            p = self.compile(source)
            with self.assertRaisesRegex(RuntimeError, 'BRK'):
                p.run()

    def test_diagnostics(self):
        cases = [('x: u8 = 300\n', 'does not fit in u8'),
                 ('x: u8 = 255 + 1\n', 'does not fit in u8'),
                 ('x = missing\n', 'unknown variable'),
                 ('if True:\n\tx = 1\n', 'tabs'),
                 ('x = eval("1")\n', 'unsupported or unknown function'),
                 ('a: array[u8, 2] = array(u8, 2)\nx = a[2]\n', 'out of bounds'),
                 ('def f(a: u8) -> u8:\n    return f(a)\n', 'recursion'),
                 ('def f(a: u8) -> u8:\n    if a:\n        return a\n', 'every path'),
                 ('from c64ultimate import turbo\n', "requires target 'c64-ultimate'"),
                 ('return 1\n', 'outside a function'),
                 ('break\n', 'outside a loop'),
                 ('for i in range(1, 2, 0):\n    pass\n', 'step cannot be zero')]
        cases += [('x = 65535 * 65535\n', '16-bit integer range'),
                  ('a: u16 = 65535\nb: i16 = -1\nx: bool = a < b\n', 'explicit cast')]
        for source, message in cases:
            with self.subTest(source=source):
                self.reject(source, message)

    def test_listing_check_and_profiles(self):
        result, listing, _ = self.invoke('x: u8 = 5\nx += 1\n', 'asm')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('inc L', listing.read_text())
        result, output, _ = self.invoke('x = 5\n', 'check')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(output.exists())
        for target in ['c64', 'c64-reu', 'c64-ultimate']:
            p = self.compile('x = 20 * 4\n', ['--target', target])
            p.run()
            self.assertEqual(p.value('x'), 80)

    def test_demo(self):
        p = self.compile((ROOT / 'examples' / 'joystick' / 'main.py').read_text())
        p.run(stop=lambda p: p.cpu.memory[0x0400 + 12*40 + 20] == 0)
        self.assertEqual(p.value('x'), 20)
        self.assertEqual(p.value('y'), 12)
        p.cpu.memory[0xDC00] = 0xFB
        p.run(stop=lambda p: p.value('x') == 19)
        self.assertEqual(p.value('x'), 19)

    def test_module_namespaces_aliases_and_exports(self):
        self.module('left.py', 'value: u16 = 10\n'
                    'def add(a: u16) -> u16:\n    return a + value\n'
                    'def unused() -> u16:\n    return 999\n')
        self.module('right.py', 'value: u16 = 20\n'
                    'def add(a: u16) -> u16:\n    return a + value\n')
        p = self.compile('import left as first, right\nfrom left import add as left_add, value as imported\n'
                         'value: u16 = 100\n'
                         'def add(a: u16) -> u16:\n    return a + value\n'
                         'total: u16 = first.add(right.add(3))\n'
                         'local: u16 = add(left_add(imported))\n'
                         'read: u16 = right.value + first.value\n')
        p.run()
        self.assertEqual([p.value(s) for s in ['total', 'local', 'read']], [33, 120, 30])
        self.assertEqual([p.value(s) for s in ['left.value', 'right.value', 'value']], [10, 20, 100])
        mapping = (self.dir / 'main.map').read_text()
        self.assertIn('function left.add ', mapping)
        self.assertNotIn('function left.unused ', mapping)

    def test_dependency_initialization_once_and_reexports(self):
        self.module('base.py', 'poke(49152, peek(49152) + 1)\nvalue: u8 = 7\n'
                    'def number() -> u8:\n    return value\n')
        self.module('one.py', 'from base import number as get\nx: u8 = get()\n')
        self.module('two.py', 'import base\nx: u8 = base.number()\n')
        p = self.compile('from one import get\nimport two\nimport base as again\n'
                         'result: u8 = get() + two.x\n')
        p.run()
        self.assertEqual(p.cpu.memory[49152], 1)
        self.assertEqual(p.value('result'), 14)
        p.restart()
        p.run()
        self.assertEqual(p.cpu.memory[49152], 2)
        mapping = (self.dir / 'main.map').read_text()
        self.assertEqual(mapping.count('module base '), 1)

    def test_dotted_modules_search_paths_and_builtin_module_import(self):
        self.module('package/helper.py', 'def number() -> u8:\n    return 42\n')
        extra = self.dir / 'extra modules'
        self.module('extra modules/other.py', 'value: u8 = 3\n')
        p = self.compile('import package.helper\nimport package.helper as helper\n'
                         'import c64.video as video\nimport other\n'
                         'result: u8 = package.helper.number() + other.value\n'
                         'video.screen.border = helper.number()\n', ['-I', str(extra)])
        p.run()
        self.assertEqual(p.value('result'), 45)
        self.assertEqual(p.cpu.memory[0xD020], 42)

    def test_module_imports_are_isolated(self):
        self.module('first.py', 'from c64.video import screen as device\n'
                    'def update():\n    device.border = 6\n')
        self.module('second.py', 'from c64.input import joystick as device\n'
                    'def pressed() -> bool:\n    return device.left\n')
        p = self.compile('import first, second\nfirst.update()\nvalue: bool = second.pressed()\n')
        p.cpu.memory[0xDC00] = 0xFB
        p.run()
        self.assertEqual(p.cpu.memory[0xD020], 6)
        self.assertEqual(p.value('value'), 1)

    def test_imported_arrays_and_string_returns(self):
        self.module('data.py', 'buffer: array[u8, 4] = array(u8, 4)\nbuffer[2] = 7\n'
                    'def message() -> str:\n    return "module works"\n')
        p = self.compile('from data import buffer, message\n'
                         'buffer[1] = 9\nx: u8 = buffer[2] + buffer[1]\nprint(message())\n')
        p.run()
        self.assertEqual(p.value('x'), 16)
        self.assertEqual(p.output, b'MODULE WORKS\r')

    def test_module_diagnostics(self):
        self.reject('import nonexistent\n', "cannot find module 'nonexistent'")
        self.module('empty.py', '')
        self.reject('from empty import missing\n', "has no export 'missing'")
        self.module('a.py', 'import b\n')
        self.module('b.py', 'import a\n')
        result, _, _ = self.invoke('import a\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('circular import', result.stderr)
        self.assertIn('b.py:1:1:', result.stderr)
        self.module('broken.py', 'def valid() -> u8:\n    return 1\n\nx: u8 = 300\n')
        result, _, _ = self.invoke('import broken\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('broken.py:5:9:', result.stderr)
        self.assertIn('x: u8 = 300', result.stderr)
        self.module('broken.py', 'def function(\n')
        result, _, _ = self.invoke('import broken\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('broken.py:', result.stderr)
        self.module('valid.py', 'value = 1\n')
        self.reject('from valid import value\nvalue = 2\n', 'conflicts with import alias')
        self.reject('if True:\n    import valid\n', 'must be at module scope')

    def test_imported_source_protection(self):
        imported = self.module('helper.py', 'value = 7\n')
        source = self.module('main.py', 'import helper\n')
        for flag in ['-o', '--map']:
            result = subprocess.run([str(COMPILER), 'build', str(source), flag, str(imported)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(imported.read_text(), 'value = 7\n')

    def test_module_resource_limits_and_file_boundaries(self):
        self.module('trailing.py', 'value: u8 = 9')
        self.module('blank.py', '')
        p = self.compile('import trailing, blank\nresult: u8 = trailing.value\n')
        p.run()
        self.assertEqual(p.value('result'), 9)
        for i in range(33):
            self.module(f'm{i}.py', f'import m{i+1}\n' if i < 32 else 'pass\n')
        self.reject('import m0\n', 'module arena exhausted', diagnostic='m30.py')
        self.module('large.py', '# ' + 'a'*33000 + '\n')
        self.reject('import large\n# ' + 'b'*33000 + '\n', 'unable to load source')

    def test_stdlib_math(self):
        p = self.compile('from pyc64.math import min_u8, max_u16, clamp_u8\n'
                         'minimum: u8 = min_u8(42, 7)\n'
                         'maximum: u16 = max_u16(65535, 32768)\n'
                         'below: u8 = clamp_u8(0, 5, 10)\n'
                         'inside: u8 = clamp_u8(7, 5, 10)\n'
                         'above: u8 = clamp_u8(255, 5, 10)\n')
        p.run()
        self.assertEqual([p.value(s) for s in ['minimum', 'maximum', 'below', 'inside', 'above']], [7, 65535, 5, 7, 10])

    def test_unused_library_frames_are_removed(self):
        self.module('library.py', 'def used() -> u8:\n    return 7\n'
                    'def unused():\n    big: array[u8, 16000] = array(u8, 16000)\n'
                    '    for i in range(10):\n        big[i] = 1\n')
        p = self.compile('from library import used\nresult: u8 = used()\n')
        p.run()
        self.assertEqual(p.value('result'), 7)
        self.assertNotIn('library.unused.big', p.symbols)
        self.assertLess((self.dir / 'main.PRG').stat().st_size, 1000)

    def test_named_constants_and_static_sizes(self):
        p = self.compile('START: Final[i16] = -3\nSTOP: Final = 4\n'
                         'COUNT: Final[u8] = STOP - START\n'
                         'ENABLED: Final[bool] = COUNT == 7\n'
                         'values: array[i16, COUNT] = array(i16, COUNT)\ntotal: i16 = 0\n'
                         'for i in range(START, STOP):\n'
                         '    values[i - START] = i\n    total += i\n'
                         'length: u16 = len(values)\n'
                         'last: i16 = values[COUNT - 1]\n'
                         'enabled: bool = ENABLED\n')
        p.run()
        self.assertEqual([p.value(s) for s in ['total', 'length', 'last', 'enabled']], [0, 7, 3, 1])
        self.assertEqual(p.symbols['values'][1], 14)
        mapping = (self.dir / 'main.map').read_text()
        self.assertIn('constant COUNT u8 7', mapping)
        self.assertNotIn('COUNT', p.symbols)

    def test_constant_strings_casts_and_signed_folding(self):
        p = self.compile('MESSAGE: Final[str] = "constants work"\n'
                         'COPY: Final = MESSAGE\nSIZE: Final = len(COPY)\n'
                         'LARGE: Final = 255\nWRAPPED: Final = u8(LARGE + 1)\n'
                         'NEGATIVE: Final = i8(LARGE)\n'
                         'SEVEN: Final = 7\nTHREE: Final = 3\n'
                         'QUOTIENT: Final = -SEVEN // THREE\nREMAINDER: Final = -SEVEN % THREE\n'
                         'SHIFT: Final = -SEVEN >> 1\nFLAG: Final = bool(123)\n'
                         'def message() -> str:\n    return COPY\n'
                         'length: u16 = SIZE\nwrapped: u8 = WRAPPED\n'
                         'negative: i8 = NEGATIVE\nquotient: i8 = QUOTIENT\n'
                         'remainder: u8 = REMAINDER\nshifted: i8 = SHIFT\nflag: bool = FLAG\n'
                         'runtime_wrap: u8 = u8(255) + u8(1)\n'
                         'runtime_complement: u8 = ~u8(0)\n'
                         'print(message())\n')
        p.run()
        self.assertEqual([p.value(s) for s in ['length', 'wrapped', 'negative', 'quotient', 'remainder', 'shifted', 'flag']],
                         [14, 0, -1, -3, 2, -4, 1])
        self.assertEqual(p.output, b'CONSTANTS WORK\r')
        self.assertEqual(p.value('runtime_wrap'), 0)
        self.assertEqual(p.value('runtime_complement'), 255)

    def test_imported_constants_and_namespace_isolation(self):
        self.module('settings.py', 'SIZE: Final[u8] = 4\nMESSAGE: Final = "imported constant"\n')
        self.module('exports.py', 'from settings import SIZE as COUNT, MESSAGE\n')
        p = self.compile('from exports import COUNT, MESSAGE\nimport settings\n'
                         'SIZE: Final = 2\nvalues: array[u8, COUNT + SIZE] = array(u8, COUNT + SIZE)\n'
                         'total: u16 = 0\n'
                         'for i in range(settings.SIZE):\n    total += COUNT\n'
                         'length: u16 = len(values)\nprint(MESSAGE)\n')
        p.run()
        self.assertEqual([p.value(s) for s in ['total', 'length']], [16, 6])
        self.assertEqual(p.output, b'IMPORTED CONSTANT\r')
        self.assertNotIn('settings.SIZE', p.symbols)
        self.reject('import settings\nsettings.SIZE = 9\n', 'cannot assign to constant')
        self.reject('import settings\nsettings.SIZE += 1\n', 'cannot assign to constant')

    def test_constant_diagnostics(self):
        cases = [('SIZE: Final = 4\nSIZE = 9\n', 'cannot assign to constant'),
                 ('SIZE: Final = 4\nSIZE += 1\n', 'cannot assign to constant'),
                 ('SIZE: Final = 4\nfor SIZE in range(2):\n    pass\n', 'loop variable'),
                 ('SIZE: Final = 4\nSIZE: Final = 5\n', 'duplicate declaration'),
                 ('value = 4\nSIZE: Final = value\n', 'compile-time value'),
                 ('SIZE: Final = peek(49152)\n', 'compile-time value'),
                 ('SIZE: Final[u8] = 256\n', 'does not fit in u8'),
                 ('BASE: Final = 65535\nSIZE: Final = BASE + 1\n', '16-bit integer range'),
                 ('ZERO: Final = 0\nSIZE: Final = 1 // ZERO\n', 'division by zero'),
                 ('SHIFT: Final = 16\nSIZE: Final = 1 << SHIFT\n', 'shift count'),
                 ('SIZE: Final = LATER\nLATER: Final = 4\n', 'unknown variable'),
                 ('if True:\n    SIZE: Final = 4\n', 'module scope'),
                 ('def f():\n    SIZE: Final = 4\n', 'module scope'),
                 ('SIZE: Final[array] = 4\n', 'constant type'),
                 ('SIZE: Final[u8]\n', 'requires a value'),
                 ('size = 4\nvalues: array[u8, size] = array(u8, size)\n', 'compile-time integer'),
                 ('SIZE: Final = 0\nvalues: array[u8, SIZE] = array(u8, SIZE)\n', '1..16384'),
                 ('SIZE: Final = 2\nvalues: array[u8, SIZE] = array(u8, SIZE)\nx = values[SIZE]\n', 'out of bounds')]
        for source, message in cases:
            with self.subTest(source=source):
                self.reject(source, message)
        self.module('broken.py', 'SMALL: Final[u8] = 300\n')
        self.reject('import broken\n', 'does not fit in u8', diagnostic='broken.py')

    def test_numeric_constants_do_not_allocate_runtime_storage(self):
        self.compile('x: u8 = 7\n')
        baseline = (self.dir / 'main.PRG').read_bytes()
        self.compile('UNUSED: Final[u16] = 65535\nVALUE: Final[u8] = 7\nx: u8 = VALUE\n')
        self.assertEqual((self.dir / 'main.PRG').read_bytes(), baseline)

    def test_constants_in_function_frames_and_cast_bounds(self):
        p = self.compile('COUNT: Final[u8] = 4\nSTEP: Final = 1\n'
                         'def calculate() -> u16:\n'
                         '    values: array[u8, u8(COUNT + 1)] = array(u8, u8(COUNT + 1))\n'
                         '    total: u16 = 0\n'
                         '    for i in range(u8(0), COUNT + 1, STEP):\n'
                         '        values[i] = u8(i)\n    for j in range(COUNT + 1):\n'
                         '        total += values[j]\n    return total\n'
                         'def shadow(COUNT: u8) -> u8:\n    return COUNT\n'
                         'result: u16 = calculate()\nlocal: u8 = shadow(9)\n')
        p.run()
        self.assertEqual([p.value(s) for s in ['result', 'local']], [10, 9])
        self.assertEqual(p.symbols['calculate.values'][1], 5)

    def test_final_and_type_import_aliases(self):
        p = self.compile('import typing as hints\nimport pyc64.types as types\n'
                         'from typing import Final as Frozen\n'
                         'from pyc64.types import u8 as Byte\n'
                         'COUNT: hints.Final[Byte] = 4\nVALUE: Frozen = 9\n'
                         'def convert(value: types.u16) -> Byte:\n    return Byte(value)\n'
                         'values = types.array(Byte, COUNT)\nvalues[3] = convert(265)\n'
                         'result: Byte = values[3] + VALUE\n')
        p.run()
        self.assertEqual(p.value('result'), 18)
        self.assertEqual(p.symbols['values'][1], 4)

    def test_python_operand_boolean_operators(self):
        p = self.compile('from c64.memory import poke\n'
                         'def effect() -> u16:\n    poke(49152, 1)\n    return 1234\n'
                         'zero: u16 = 0\nseven: u16 = 7\nlarge: u16 = 513\n'
                         'a: u16 = zero and effect()\nb: u16 = seven or effect()\n'
                         'c: u16 = seven and large\nd: u16 = zero or large\n'
                         'e: u16 = 7 and 513\nf: u16 = 7 or 513\n'
                         'negative: i16 = -7\ng: i16 = negative or i16(8)\n'
                         'inverse: bool = not seven\n'
                         'SAFE: Final = False and (1 // 0)\n'
                         'OTHER: Final = 7 or (1 << 16)\n'
                         'safe: bool = SAFE\nother: u8 = OTHER\n')
        p.run()
        self.assertEqual([p.value(s) for s in ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'inverse', 'safe', 'other']],
                         [0, 7, 513, 513, 513, 7, -7, 0, 0, 7])
        self.assertEqual(p.cpu.memory[49152], 0)

    def test_explicit_initialization_and_unbound_reads(self):
        cases = [('x: u8\n', 'annotation alone'),
                 ('a: array[u8, 4]\n', 'annotation alone'),
                 ('flag: bool = True\nif flag:\n    x = 7\ny = x\n', 'before initialization'),
                 ('for i in range(0):\n    x = 7\ny = x\n', 'before initialization'),
                 ('flag: bool = False\nwhile flag:\n    x = 7\nx += 1\n', 'before initialization'),
                 ('x = 7\ndef bad() -> u8:\n    y = x\n    x = 9\n    return y\n', 'before initialization'),
                 ('x = 7\ndef bad() -> u8:\n    x = x + 1\n    return x\n', 'before initialization'),
                 ('a: array[u8, 4] = array(u8, 3)\n', 'does not match'),
                 ('array(u8, 4)\n', 'assigned to a variable')]
        for source, message in cases:
            with self.subTest(source=source):
                self.reject(source, message)
        p = self.compile('flag: bool = True\nif flag:\n    value = 7\nelse:\n    value = 9\nresult = value\n'
                         'values = array(u8, 3)\nvalues[1] = 9\nvalues = array(u8, 3)\nread = values[1]\n')
        p.run()
        self.assertEqual([p.value(s) for s in ['result', 'read']], [7, 0])

    def test_rejects_invented_syntax_and_implicit_library_globals(self):
        cases = [('const COUNT: u8 = 10\n', 'end of statement'),
                 ('x: u8 = 1\n', 'import fixed-width types'),
                 ('x = u8(1)\n', 'unknown function'),
                 ('x = peek(49152)\n', 'unknown function'),
                 ('COUNT: Final = 1\n', 'unsupported type'),
                 ('class = 1\n', 'Python keyword'),
                 ('def f(if: bool):\n    pass\n', 'Python keyword')]
        for source, message in cases:
            with self.subTest(source=source):
                result, output, _ = self.invoke(source, bindings=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertFalse(output.exists())

    def test_examples_are_valid_python_syntax_and_compile(self):
        for source in sorted((ROOT / 'examples').glob('*/main.py')):
            with self.subTest(source=source):
                ast.parse(source.read_text(), filename=str(source))
                project = source.with_name('pyc64.toml')
                inputs = ['--project', str(project)] if project.exists() else [str(source)]
                result = subprocess.run([str(COMPILER), 'check', *inputs], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
        ast.parse((ROOT / 'stdlib/pyc64/math.py').read_text())
        for source in [ROOT / 'stdlib/c64/sid.py', ROOT / 'stdlib/c64/audio.py',
                       ROOT / 'stdlib/c64/disk.py', ROOT / 'stdlib/c64/sprite.py',
                       ROOT / 'stdlib/c64/_sprite.py']:
            ast.parse(source.read_text(), filename=str(source))

    def test_python_library_imports_and_explicit_array_conversions(self):
        result = subprocess.run([sys.executable, '-c',
            'from typing import Final; from pyc64.types import u8, i8, u16, i16, array; '
            'from pyc64.math import clamp_u8; '
            'assert [u8(256), i8(255), u16(-1), i16(65535)] == [0, -1, 65535, -1]; '
            'values: array[u8, 3] = array(u8, 3); values[1] = 265; '
            'assert len(values) == 3 and values[0] == 0 and values[1] == 9; '
            'assert clamp_u8(u8(100), u8(5), u8(10)) == 10; '
            'real_values = array(float, 2); real_values[1] = 1.25; '
            'assert real_values[0] == 0.0 and real_values[1] == 1.25'],
            cwd=ROOT / 'stdlib', text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()

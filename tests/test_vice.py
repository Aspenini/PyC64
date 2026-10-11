"""Optional integration smoke test against a complete C64 in VICE."""
import argparse
import math
from pathlib import Path
import re
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler', type=Path)
    parser.add_argument('vice', type=Path)
    inputs = parser.add_mutually_exclusive_group()
    inputs.add_argument('--source', type=Path)
    inputs.add_argument('--project', type=Path, help='build through the native TOML project workflow')
    parser.add_argument('--text', action='append', help='expected text in screen RAM; repeat for multiple strings')
    parser.add_argument('--expect', action='append', metavar='NAME=VALUE',
                        help='expected scalar global; repeat for multiple values (default: total=450)')
    parser.add_argument('--sid-registers', help='expected 25 write-only SID registers as hexadecimal bytes')
    parser.add_argument('--stop-at-function', help='inspect live hardware before entering a mapped function; requires --runs 1')
    parser.add_argument('--audio-output', type=Path, help='record emulated SID audio to WAV and require non-silent PCM output')
    parser.add_argument('--video-standard', choices=['pal', 'ntsc'],
                        help='select the emulated C64 video standard for machine-clock checks')
    parser.add_argument('--target', choices=['c64', 'c64-reu', 'c64-ultimate'],
                        help='override the compiler target profile')
    parser.add_argument('--reu-size', type=int, choices=[128, 256, 512, 1024, 2048, 4096, 8192, 16384],
                        help='attach a temporary REU of this size in KiB')
    parser.add_argument('--reu-bytes', action='append', metavar='ADDRESS=HEX',
                        help='verify bytes in the saved REU image; requires --reu-size')
    parser.add_argument('--asset-bytes', action='append', metavar='BASENAME[:MODE]=HEX',
                        help='verify a complete linked payload; use a mode when a PNG produces several assets')
    parser.add_argument('--screen-bytes', action='append', metavar='OFFSET=HEX',
                        help='verify raw bytes at an offset in the 1000-byte text screen')
    parser.add_argument('--color-bytes', action='append', metavar='OFFSET=HEX',
                        help='verify color nibbles at an offset in the 1000-byte color RAM')
    parser.add_argument('--limit-cycles', type=int,
                        help='override the VICE cycle budget for larger demos')
    parser.add_argument('--disk-roundtrip', action='store_true',
                        help='attach a temporary blank D64 and verify the disk demo file')
    parser.add_argument('--disk-file', default='pyc64-demo',
                        help='sequential file to extract with --disk-roundtrip')
    parser.add_argument('--disk-bytes', default='00 ff 2a',
                        help='expected file bytes in hexadecimal with --disk-roundtrip')
    parser.add_argument('--runs', type=int, default=1,
                        help='number of SYS-equivalent runs before checking memory (default: 1)')
    parser.add_argument('--keys', help='preload up to ten ASCII keyboard bytes per run; newline becomes RETURN')
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be at least 1')
    if args.stop_at_function and args.runs != 1:
        parser.error('--stop-at-function requires --runs 1')
    if args.reu_bytes and not args.reu_size:
        parser.error('--reu-bytes requires --reu-size')
    if args.limit_cycles is not None and args.limit_cycles < 1:
        parser.error('--limit-cycles must be positive')
    keys = b''
    if args.audio_output:
        args.audio_output = args.audio_output.resolve()
        args.audio_output.parent.mkdir(parents=True, exist_ok=True)
    if args.keys is not None:
        try:
            keys = args.keys.replace('\n', '\r').encode('ascii')
        except UnicodeEncodeError:
            parser.error('--keys requires ASCII')
        if len(keys) > 10:
            parser.error('--keys exceeds the ten-byte KERNAL keyboard queue')
    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / 'build') as tmp:
        directory = Path(tmp)
        disk_options = []
        reu_options = []
        if args.reu_size:
            reu_image = directory / 'memory.reu'
            reu_image.write_bytes(bytes(args.reu_size * 1024))
            reu_options = ['-reu', '-reusize', str(args.reu_size), '-reuimage', str(reu_image), '-reuimagerw']
        if args.disk_roundtrip:
            image = directory / 'test.d64'
            c1541 = args.vice.resolve().with_name('c1541.exe' if args.vice.suffix == '.exe' else 'c1541')
            subprocess.run([str(c1541), '-format', 'pyc64-test,pc', 'd64', str(image)],
                           check=True, capture_output=True, timeout=15)
            disk_options = ['-drive8truedrive', '-8', str(image)]
        prg, mapping = directory / 'test.PRG', directory / 'test.map'
        build_input = (['--project', str(args.project.resolve())] if args.project else
                       [str((args.source or ROOT / 'examples/language/main.py').resolve())])
        subprocess.run([str(args.compiler.resolve()), 'build', *build_input,
                        *(['--target', args.target] if args.target else []),
                        '-I', str(ROOT / 'stdlib'),
                        '-o', str(prg), '--map', str(mapping)], check=True)
        stop_address = '0203'
        if args.stop_at_function:
            functions = {f[1]: f[2][1:] for line in mapping.read_text().splitlines()
                         if (f := line.split()) and f[0] == 'function'}
            if args.stop_at_function not in functions:
                raise RuntimeError('stop function is not linked: ' + args.stop_at_function)
            stop_address = functions[args.stop_at_function]
        screen, ram = directory / 'screen.bin', directory / 'ram.bin'
        colors = directory / 'colors.bin'
        monitor_log = directory / 'monitor.log'
        vice_log = directory / 'vice.log'
        commands = directory / 'commands.mon'
        run_commands = []
        for _ in range(args.runs):
            if keys:
                run_commands.extend(['> 0277 ' + keys.hex(' '), f'> 00c6 {len(keys):02x}'])
            run_commands.append('g 0200')
        # A monitor-installed JSR supplies a valid return stack, matching SYS.
        commands.write_text('\n'.join([
            f'logname "{monitor_log.as_posix()}"',
            'log on',
            f'load "{prg.as_posix()}" 0',
            '> 0200 20 0d 08 ea',
            'break exec ' + stop_address,
            *run_commands,
            f'bsave "{screen.as_posix()}" 0 0400 07e7',
            *([f'bsave "{colors.as_posix()}" 0 d800 dbe7'] if args.color_bytes else []),
            f'bsave "{ram.as_posix()}" 0 0801 9fff',
            *(['io d400'] if args.sid_registers else []),
            'quit', '']), encoding='ascii')
        sound_options = (['-sound', '-sounddev', 'wav', '-soundoutput', '1', '-soundwarpmode', '1',
                          '-soundarg', str(args.audio_output)]
                         if args.audio_output else ['+sound'])
        result = subprocess.run([str(args.vice.resolve()), '-default', *sound_options,
                                 *([] if args.audio_output else ['-warp']),
                                 '-logfile', str(vice_log),
                                 *(['-' + args.video_standard] if args.video_standard else []),
                                 *disk_options,
                                 *reu_options,
                                 '-initbreak', 'ready', '-moncommands', str(commands),
                                 '-limitcycles', str(args.limit_cycles or (30000000 if args.disk_roundtrip or args.reu_size else 3000000))],
                                capture_output=True, timeout=60 if args.disk_roundtrip else 30)
        if not screen.exists() or not ram.exists():
            diagnostics = (result.stdout + result.stderr).decode(errors='replace')
            if vice_log.exists():
                diagnostics += '\n' + vice_log.read_text(errors='replace')
            raise RuntimeError(f'VICE did not complete monitor test (exit {result.returncode}):\n' +
                               diagnostics[-3000:])
        memory = ram.read_bytes()
        if args.asset_bytes:
            assets = [line.split(maxsplit=5) for line in mapping.read_text().splitlines()
                      if line.startswith('asset ')]
            for check in args.asset_bytes:
                name, wanted = check.split('=', 1)
                basename, separator, mode = name.rpartition(':')
                if not separator:
                    basename, mode = name, ''
                matches = [fields for fields in assets if Path(fields[5]).name == basename and
                           (not mode or fields[4] == 'mode=' + mode)]
                assert len(matches) == 1, f'asset {name}: expected one mapped payload, found {len(matches)}'
                _, address, count, alignment, mode, path = matches[0]
                start, expected_bytes = int(address[1:], 16) - 0x0801, bytes.fromhex(wanted)
                assert len(expected_bytes) == int(count), f'asset {name}: check must cover its full payload'
                actual_bytes = memory[start:start + int(count)]
                assert actual_bytes == expected_bytes, f'asset {name}: linked payload differs in VICE RAM'
        symbols = {fields[1]: fields[2:] for line in mapping.read_text().splitlines()
                   if (fields := line.split()) and fields[0] == 'variable'}
        checks = args.expect or ['total=450']
        for check in checks:
            name, wanted = check.split('=', 1)
            address, size, kind = symbols[name]
            start, width = int(address[1:], 16) - 0x0801, int(size)
            if kind == 'float':
                data = memory[start:start + 5]
                fraction = ((data[1] & 127) << 24) | (data[2] << 16) | (data[3] << 8) | data[4]
                actual = 0.0 if not data[0] else (-1 if data[1] & 128 else 1) * (1 + fraction / 2147483648) * 2.0 ** (data[0] - 129)
                assert math.isclose(actual, float(wanted), rel_tol=1e-8, abs_tol=1e-38), f'{name}: expected {wanted}, got {actual}'
                continue
            assert width in (1, 2) and kind not in ('array', 'list'), f'{name} is not a scalar'
            actual = int.from_bytes(memory[start:start + width], 'little', signed=kind.startswith('i'))
            assert actual == int(wanted, 0), f'{name}: expected {wanted}, got {actual}'
        # KERNAL translates PETSCII letters to uppercase screen codes.
        texts = args.text if args.text is not None else ([] if args.screen_bytes or args.color_bytes else ['NATIVE CODE WORKS'])
        for text in texts:
            expected = bytes(ch-64 if 64 <= ch <= 95 else ch for ch in text.upper().encode('ascii'))
            assert expected in screen.read_bytes(), f'KERNAL output missing from screen RAM: {text}'
        for label, entries, contents in [('screen', args.screen_bytes, screen.read_bytes()),
                                          ('color RAM', args.color_bytes, bytes(value & 15 for value in colors.read_bytes()) if args.color_bytes else b'')]:
            for check in entries or []:
                offset, wanted = check.split('=', 1)
                start, expected = int(offset, 0), bytes.fromhex(wanted)
                assert expected and 0 <= start <= 1000 - len(expected), f'{label} check outside display memory'
                assert contents[start:start + len(expected)] == expected, f'{label} bytes at offset {start} differ'
        if args.sid_registers:
            expected_sid = bytes.fromhex(args.sid_registers)
            assert len(expected_sid) == 25, 'expected exactly 25 SID register bytes'
            # Monitor's chip dump exposes latched values; CPU reads do not.
            dump = monitor_log.read_text(errors='replace')
            actual_sid = bytearray()
            for line in dump.splitlines():
                match = re.search(r'>C:([0-9a-f]{4})\s+((?:[0-9a-f]{2}\s+){15}[0-9a-f]{2})',
                                  line, re.IGNORECASE)
                if match and int(match[1], 16) == 0xD400 + len(actual_sid):
                    actual_sid.extend(bytes.fromhex(match[2]))
            assert actual_sid[:25] == expected_sid, f'SID registers differ: {actual_sid.hex(" ")}\n{dump[-5000:]}'
        if args.disk_roundtrip:
            exported = directory / 'disk-data.bin'
            extraction = subprocess.run([str(c1541), str(image), '-read', args.disk_file + ',s', str(exported)],
                                        capture_output=True, timeout=15)
            if extraction.returncode:
                listing = subprocess.run([str(c1541), str(image), '-dir'],
                                         capture_output=True, timeout=15)
                raise RuntimeError('c1541 extraction failed:\n' +
                                   (extraction.stdout + extraction.stderr + listing.stdout + listing.stderr)
                                   .decode(errors='replace'))
            actual_bytes, expected_bytes = exported.read_bytes(), bytes.fromhex(args.disk_bytes)
            assert actual_bytes == expected_bytes, ('1541 disk file has wrong bytes: '
                                                    f'expected {expected_bytes.hex(" ")}, got {actual_bytes.hex(" ")}')
        if args.reu_bytes:
            expansion = reu_image.read_bytes()
            for check in args.reu_bytes:
                address, wanted = check.split('=', 1)
                start, expected_bytes = int(address, 0), bytes.fromhex(wanted)
                assert 0 <= start <= len(expansion) - len(expected_bytes), 'REU check outside attached memory'
                actual_bytes = expansion[start:start + len(expected_bytes)]
                assert actual_bytes == expected_bytes, (f'REU bytes at {start:#x}: '
                                                        f'expected {expected_bytes.hex(" ")}, got {actual_bytes.hex(" ")}')
        if args.audio_output:
            try:
                with wave.open(str(args.audio_output), 'rb') as recording:
                    assert recording.getnframes() > 0 and recording.getsampwidth() == 2, 'expected 16-bit SID audio'
                    samples = recording.readframes(recording.getnframes())
                    assert any(samples), 'recorded SID output is silent'
            except (wave.Error, AssertionError) as error:
                raise RuntimeError(str(error) + '\n' + vice_log.read_text(errors='replace')[-4000:]) from error
        print('VICE C64 integration passed: ' + ', '.join(checks) +
              (', KERNAL screen output verified' if texts else '') +
              (', raw display memory verified' if args.screen_bytes or args.color_bytes else '') +
              (', latched SID registers verified' if args.sid_registers else '') +
              (', 1541 disk file bytes verified' if args.disk_roundtrip else '') +
              (', REU image bytes verified' if args.reu_bytes else '') +
              (', SID audio WAV verified' if args.audio_output else '') +
              (', linked asset payloads verified' if args.asset_bytes else ''))


if __name__ == '__main__':
    main()

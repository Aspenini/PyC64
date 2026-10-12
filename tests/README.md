# PyC64 tests

`xmake test` runs everything; each check below is a separate xmake test.
Nothing is generated in advance: every run compiles the sources here with
`pyc64c` and executes the PRG on the shipped emulators.

```sh
xmake test                              # all tests
xmake test "pyc64_tests/language_*"     # one area
xmake test "pyc64_tests/machine_chips_*"
```

A failing test prints the command, its output and a workspace under
`build/tests/NAME/` that is kept for inspection.

| Folder | Test name | What runs |
| --- | --- | --- |
| `programs/AREA/NAME.py` | `AREA_NAME` | Compile, then run on `pyc64test` (chips 6502) |
| `programs/AREA/NAME/main.py` | `AREA_NAME` | Same, with the folder's other modules |
| `errors/AREA.txt` | `error_AREA` | Several programs that must fail to compile |
| `machine/NAME.py` | `machine_chips_NAME`, `machine_chrono_NAME` | Full PAL C64 on both emulators |
| `host/NAME.py` | `host_NAME` | The `stdlib/` Python implementations under CPython |
| `cli.lua` | `cli_NAME` | Command-line behaviour of the three tools |
| `../examples/NAME/` | `example_NAME` | Every example compiles |

Tests that need C64 ROMs or CPython are skipped (and counted) until they are
configured: `xmake f --basic_rom=... --kernal_rom=...` (a `chargen` ROM beside
them, or `--chargen_rom`, enables machine tests) and `--python=...`.
`PYC64_BASIC_ROM`, `PYC64_KERNAL_ROM` and `PYC64_CHARGEN_ROM` also work.

## Programs

A program states its expected output in `#>` comments, in order. The C64
prints letters in uppercase, so expected text is uppercase too:

```python
# oracle: cpython
from pyc64.types import u8
total: u8 = 200 + 55
print("total", total)
#> TOTAL 255
```

Directives are comments of the form `# NAME: VALUE`:

| Directive | Meaning |
| --- | --- |
| `#> TEXT` | Next expected output line (`#>` alone is an empty line) |
| `# oracle: cpython` | Also run the source under CPython; its uppercased output must match |
| `# expect: ADDRESS=BYTE` | Check RAM after the run (repeatable); `$C000`, `0xC000` or decimal |
| `# fails: TEXT` | The program must stop with TEXT on stderr, e.g. `BRK guard` |
| `# requires: roms` | Map the BASIC and KERNAL ROMs (floats and `math`) |
| `# pyc64c: ARGS` | Extra compiler arguments, e.g. `--target c64-reu` |
| `# pyc64test: ARGS` | Extra tester arguments, e.g. `--input "YES\r"` |
| `# source: PATH` | Run a program kept elsewhere, e.g. `examples/hello/main.py` |

`pyc64test` services CHROUT and GETIN; `--input` text may use `\r` for RETURN
and `\xHH` for any key code. RAM checks still apply when a program stops at a
guard, which shows a trap happened before a hardware write.

## Compile errors

An `errors/*.txt` file holds cases separated by `# ---`. Each case lists the
text its diagnostic must contain and, optionally, where it must point:

```python
# error: does not fit in u8
# at: main.py:2
from pyc64.types import u8
x: u8 = 300
# ---
# error: circular import
# at: b.py:1
import a
# file: a.py
import b
# file: b.py
import a
```

`# file: NAME.py` starts another source in the same case. Every case must
fail without writing a PRG and report `FILE:LINE:COLUMN: error:`.

## Machine tests

`machine/*.py` programs run on chips (`pyc64test --machine`) and on Chrono
(`pyc64run --headless`) after a normal BASIC boot:

| Directive | Meaning |
| --- | --- |
| `# frames: N` | PAL frames to run (default 120) |
| `# screen: TEXT` | Text that must be on the screen (repeatable) |
| `# expect: ADDRESS=BYTE` | RAM check (repeatable) |
| `# input: TEXT` / `# key: CODE` / `# joystick: MASK` | Keyboard and joystick 2 |
| `# build: ARGS` | Build something else, e.g. `--project ${ROOT}/examples/music_cia/pyc64.toml` |

## Host library tests

`host/*.py` scripts import the `stdlib/` modules under CPython, usually with a
small `c64.memory` adapter, and must exit successfully. Files starting with
`_` are shared helpers such as the REU model.

## Other checks

`xmake test "chrono_test_*"` runs ChronoCoders/C64's own unit suites from the
submodule. `test_vice.py` and `test_rsid_vice.py` are optional, manual
cross-checks against an installed VICE.

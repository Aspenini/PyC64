# PyC64

**A subset of Python, plus a convenient library for the Commodore 64.**

Windows x64 / Linux x86_64 · Native 6502/6510 output

Write ordinary Python syntax and compile it straight to a C64 `.PRG`. Generated
programs need no interpreter or VM. Fixed-width types and hardware access come
from libraries, rather than custom language syntax.

## Get started

Run the Windows installer (`PyC64-VERSION-windows-x64-setup.exe`). It includes the compiler, runner,
tester, libraries and examples. It installs to `C:\PyC64` and requires administrator
permission. It does not change PATH. Run the compiler using
`C:\PyC64\bin\pyc64c.exe`. You do **not** need Python or a compiler toolchain.

Save this as `main.py` in a writable folder:

```python
from pyc64.types import u16
from c64.video import screen

screen.border = 6
screen.background = 0
screen.clear()

score: u16 = 42
print("HELLO FROM PYC64!", score)
```

```powershell
C:\PyC64\bin\pyc64c.exe run main.py --rom-dir "C:\C64-ROMs"
```

`run` builds `main.PRG` and opens it in `pyc64run`, a PAL C64 with video, keyboard,
joystick and SID audio. Supply `basic.rom`, `kernal.rom` and `chargen.rom` in the
ROM folder (VICE's original filenames also work), or set `PYC64_ROM_DIR` once and
omit `--rom-dir`. ROMs are not bundled. `build` writes the PRG without running it;
you can also load it on a real C64 or in another emulator.

## Run and test

PyC64 ships three programs:

| Program | Purpose | Emulator |
| --- | --- | --- |
| `pyc64c` | Compiler and project tool | — |
| `pyc64run` | Play a PRG in a window, or check it headless | [ChronoCoders/C64](https://github.com/ChronoCoders/C64) |
| `pyc64test` | Headless machine-code tests | [floooh/chips](https://github.com/floooh/chips) |

In the `pyc64run` window, F12 closes; Escape is RUN/STOP, Page Up is RESTORE, Tab
is CTRL and Left Alt is Commodore. Numpad 8/2/4/6 and Right Ctrl control joystick 2.
Add `--headless` to run `--frames N` (default 300) without a window, then check
`--expect ADDRESS=BYTE` or `--expect-screen TEXT`.

`pyc64c run` passes `--rom-dir DIR` and anything after `--` to `pyc64run`, for
example `pyc64c run -- --headless --frames 100 --expect-screen READY`. It uses the
`pyc64run` installed beside `pyc64c`; `--runner FILE`, `PYC64_RUNNER` or a project's
`[runner] executable` select another. `pyc64c sid FILE.sid --run` works the same way.

`pyc64test` runs the PRG on chips' cycle-stepped 6502 with flat RAM by default.
CHROUT and GETIN are handled by the tester; the test passes when the program returns
within `--cycles N` and every check holds. No ROMs are needed:

```powershell
C:\PyC64\bin\pyc64test.exe MAIN.PRG --expect-text "HELLO FROM PYC64!" --expect 0xC000=42
C:\PyC64\bin\pyc64test.exe MAIN.PRG --expect-reg SP=0xF0 --trace trace.txt
C:\PyC64\bin\pyc64test.exe MAIN.PRG --machine --rom-dir C:\C64-ROMs --frames 60 --expect-screen "HELLO FROM PYC64!"
```

`--trace FILE` logs every instruction with its cycle count and registers.
`--machine` runs chips' complete PAL C64 instead, for IRQs, graphics, keyboard,
joystick and music; it needs ROMs and an `--expect` or `--expect-screen` check.
Both tools accept `--input "hello\r"`, `--entry ADDRESS` and `--ram FILE`; machine
runs also take `--key CODE`, `--joystick MASK`, `--screenshot FILE.bmp` and
`--audio FILE.wav`. Exit status is 1 when a check fails, so either tool fits a CI
script. ROMs can also come from `PYC64_ROM_DIR`. NTSC, REU and disk-image mounting
are deferred.

## Projects and commands

```powershell
C:\PyC64\bin\pyc64c.exe new mygame
cd mygame
C:\PyC64\bin\pyc64c.exe build
```

`new` creates `main.py`, `pyc64.toml` and `.gitignore`. Commands without a source
file use that configuration and its build folder. Use `--project FILE` to select
another project; configure its target and assets in TOML.

| Command | Purpose |
| --- | --- |
| `build` / `run` | Write a native PRG / build and open it in `pyc64run` |
| `check` | Check source without writing a PRG |
| `size` | Report code, data and asset sizes |
| `asm` | Write a readable native instruction listing |
| `asset` | Prepare PNG or binary graphics assets |
| `sid` | Prepare a standalone RSID player PRG |
| `clean` | Remove the project's named build outputs |

Use `C:\PyC64\bin\pyc64c.exe --help` for options. Keep the installed `bin/` and
`share/pyc64/stdlib/` together, or put `stdlib/` beside a portable executable.
Copy installed examples into a writable folder before building them.

## What you can write

| Area | Supported |
| --- | --- |
| Numbers | `u8`, `i8`, `u16`, `i16`, `bool`, C64 `float`, arithmetic, powers, bitwise operations and common `math` functions |
| Control flow | Functions with inferred or annotated types, conditionals, loops, `range`, `break`, `continue`, loop `else` and assertions |
| Data | Fixed arrays and homogeneous lists; immutable ASCII strings and binary `bytes`; indexing, iteration, byte slicing and searches |
| Structure | Compile-time imports, `typing.Final`, packed classes, methods, properties and `enum.IntEnum` |
| Helpers | Printing, keyboard `input`, string operations, seeded random generation and collection helpers |

Use explicit imports: `from pyc64.types import u8, u16, array`. Functions can
infer types from their calls; not every function needs annotations. Unsupported
Python features produce compiler errors.

## C64 libraries

| Library | Capabilities | Example |
| --- | --- | --- |
| `c64.video`, `c64.input`, `c64.keyboard` | Text display, joystick and keyboard | [Joystick](examples/joystick/main.py) |
| `c64.sprite`, `c64.assets`, `c64.tiles`, `c64.bitmap` | Sprite objects, PNG assets, tile maps and hires bitmaps | [Graphics](examples/assets/main.py) |
| `c64.sid`, `c64.audio`, `c64.music` | SID voices, tuned notes, VBI/CIA-timed PSID and native-IRQ RSID playback | [Music](examples/music/main.py) |
| `c64.disk` | KERNAL byte/buffer I/O, EOF, DOS status and commands | [Disk buffers](examples/disk_buffers/main.py) |
| `c64.irq`, `c64.memory`, `c64.reu` | Raster callbacks, direct memory access and synchronous REU DMA | [Raster IRQ](examples/raster/main.py) |

Browse [examples](examples) for complete programs. Library signatures are in
`stdlib/` source and type stubs (installed under `share/pyc64/stdlib/`).
Examples with assets have a `pyc64.toml`: build
those with `C:\PyC64\bin\pyc64c.exe build --project examples\assets\pyc64.toml`.

The default target is `c64`; `--target c64-reu` enables REU APIs. `pyc64run` does
not emulate an REU yet, so `run` warns and the program sees no expansion; test REU
programs in VICE (`x64sc -reu -reusize 512 -autostart MAIN.PRG`). `c64-ultimate`
currently provides REU access; Ultimate-specific APIs and hosting are future work.

For RSID files that need full control of the machine:

```powershell
C:\PyC64\bin\pyc64c.exe sid tune.sid --song 2 -o TUNE.PRG
```

This supports BASIC, busy-loop init, NMI and banked/direct-vector handlers. The
tune takes over the C64 and cannot resume foreground Python. It needs a free
256-byte launcher page and staging space, usually allowing up to 50,688 payload
bytes. Try the original [standalone tunes](examples/music_standalone/assets).

## Important limits

- **Static storage:** fixed-length containers; no recursion, general heap,
  dynamic/nested containers or full Python object semantics. Container arguments
  reference caller storage, but assignment aliases and container returns are unsupported.
- **Numbers:** fixed-width integers can wrap; `int` means `i16`. C64 floats have
  about nine decimal digits and need BASIC ROM; powers/transcendental math also
  need KERNAL. Results can differ from CPython.
- **Data:** 255-character input lines; separate 4 KiB input/byte-result arenas.
  No string/list/array slicing or `bytearray`. ASCII converts to PETSCII at text I/O.
- **Hardware:** normal graphics use VIC bank 0. IRQ callbacks have an 80-byte
  stack budget and cannot block, allocate or use ROM operations. Cooperative RSID
  needs returning init, a clean image and exclusive IRQ ownership.
- **Failures:** runtime guards trap with `BRK`; Python exceptions are unsupported.

Multicolor bitmaps, larger RSID loaders, MUS/multi-SID playback, higher-level disk
file objects, assembly linking and broader Python support are deferred. The
Linux builds are verified on Ubuntu under WSL 2 with GCC and Clang.
macOS and real-hardware coverage are not yet release-validated.

## Build and test

Use [xmake](https://xmake.io/) and Visual Studio's C++ build tools/Windows SDK.
Windows defaults to MSVC; `--toolchain=clang-cl` selects Clang's compatible driver.
Both use a static runtime. xmake fetches SDL2 for `pyc64run` and links it statically
on Windows; `pyc64test` has no SDL dependency. The native compiler needs no LLVM-MOS, cc65 or assembler.
The emulator cores are pinned git submodules in `third_party/`; xmake initializes
them on first build, or run `git submodule update --init` yourself.

```powershell
git clone --recurse-submodules https://github.com/Aspenini/PyC64.git
cd PyC64
xmake f -m release
xmake
xmake install -o build/cli-install
& 'C:\Program Files\Inno Setup\ISCC.exe' packaging\PyC64.iss
```

`pyc64c`, `pyc64run` and `pyc64test` are in `build/windows/x64/release/`; Inno Setup writes to
`build/installer/`. Use `xmake f -m debug` for debugging. Explicit GCC/Clang
toolchains are supported; their runtime dependencies may differ.

On Linux, install SDL2 development files (Ubuntu: `sudo apt install libsdl2-dev`),
then use GCC or Clang with xmake:

```sh
xmake f -m release --toolchain=gcc
xmake
xmake install -o "$HOME/.local"
"$HOME/.local/bin/pyc64c" --version
```

The three executables are in `build/linux/x86_64/release/`. Use `--toolchain=clang`
to select Clang. Keep the installed `bin/` and `share/` directories together.

`xmake test` compiles and runs the suite in [tests](tests): about 300 small Python
programs, compile-error cases, command-line checks and example builds, each reported
as its own test. Programs run on `pyc64test`; machine tests run the same PRG on both
`pyc64test --machine` and `pyc64run --headless`. CPython confirms that programs
marked as Python-compatible print the same output; there are no pip dependencies.

```powershell
xmake f --basic_rom=C:\C64-ROMs\basic.rom --kernal_rom=C:\C64-ROMs\kernal.rom
xmake test
xmake test "pyc64_tests/machine_*"
```

Float, math and full-machine tests need the C64 ROMs; a character ROM beside BASIC
(or `--chargen_rom`) enables machine tests. Tests whose ROMs or CPython are missing
are skipped and counted. See [tests/README.md](tests/README.md) to add a test.

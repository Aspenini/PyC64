# PyC64 Project Plan

Current implementation progress is tracked in [README.md](README.md). Version
0.1 now includes native integer/boolean/multiple-argument printing, buffered
keyboard input, basic sprite functions, and Python source SID/audio libraries
alongside the earlier compiler, module, constants and math-library milestones.
Signed floor division/remainder and runtime range arguments are also implemented.
Scalar parameter and return inference, fixed-size integer/bool/string list
literals, indexed mutation and negative indices are implemented.
Array/list parameters now reference caller storage with captured lengths and
inferred or annotated element types. The Python collections library provides
fill/copy helpers, and the disk library supports bounded binary buffer reads and
writes, verified with true 1541 emulation. Indexed assignment follows Python's
value-before-index evaluation order.
Direct list/array/string iteration and Python loop `else` are implemented,
including captured iterators, visible element mutations and immutable saved
characters. The iteration example passes CPU/CPython checks and repeated VICE runs.
Runtime string length/indexing, ASCII ord/chr, truthiness and case-sensitive
content comparisons are implemented. `pyc64.strings` adds search, prefix/suffix,
count and classification helpers in ordinary Python. Text conversion occurs at
C64 I/O boundaries, with source strings preserved, verified on a true 1541.
Python assertions and `pyc64.random` are now implemented. The random library
provides reproducible seeded 16-bit generation, bounded integers, signed ranges
and sequence choice; invalid preconditions stop before advancing generator state.
Struct-like classes now support packed scalar fields, constant defaults,
constructors, methods and inferred/annotated object references. Field
initialization and static storage restrictions are documented in README.md.
The class demo passes CPU/CPython comparisons and repeated full C64 VICE runs.
Standard Python property getters/setters and the source `c64.sprite.Sprite` API
are implemented, including live VIC-II properties, multicolor, expansion and
shared colors. Slot/bitmap guards and neighboring-bit preservation are verified
against CPU/CPython behavior and repeated VICE runs. `input()` now provides an
optional string prompt, line editing and immutable answers, with documented
line/arena limits and repeated full-machine KERNAL keyboard checks. Finite C64
five-byte floats now provide literals, conversion (including `float(input(...))`),
arithmetic/true division, comparisons, output and scalar/container value storage.
CPU checks execute the actual BASIC ROM; the numeric input demo and the user's
unchanged `kevinallen.py` pass repeated VICE runs. Powers and standard `math`
constants/functions now work, with the unchanged circle-area program verified
in VICE. Integer conversion accepts floats and decimal input; fixed-width float
casts and `math.floor`/`ceil`/`trunc` are implemented and checked in repeated VICE
runs. Native exact floating floor division/remainder and scalar division/remainder
augmented assignments now work, with decimal/sign/range/underflow/snapshot checks
and repeated VICE runs of the division/remainder example. Floating constant folding
and remaining math APIs remain planned.
The native desktop CLI implements TOML project configuration, `new`, `run` and
`clean`, with relocatable installed source libraries. PNG sprite/charset conversion
and binary embedding now work, with tile-map extraction and drawing too;
multicolor bitmap conversion and
additional optimization settings below remain planned.
KERNAL logical-file open/close and byte/text I/O, saved status/error, and Python
disk EOF/failure, DOS status-code and command helpers are implemented and checked
with true 1541 emulation, including rename and FILE NOT FOUND responses.
SID Voice handles, shared scalar properties and clock-aware Hz tuning are now
implemented, including the object/import form in section 22 and tuned note helpers.
Repeated VICE runs verify the voice demo and automatic PAL/NTSC clock selection.
Finite Python `IntEnum` definitions now provide integer constant members, auto
values, aliases, canonical names/values, guarded value/name lookup, `len` and
distinct-member iteration. Nominal enum types work in functions, fields/properties
and homogeneous containers. CPU/CPython comparisons and repeated VICE runs verify
the enum example; other enum/flag types and custom enum methods remain planned.
Immutable bytes now provide all byte values, literals/raw forms, bounded runtime
construction, integer indexing/iteration, content comparisons, quoted output,
scalar signatures and fields/properties. CPU/CPython checks and full-machine
binary disk roundtrip/repeated FILE EXISTS checks verify the bytes demo. Remaining
bytes operations and bytearray remain planned. Bytes concatenation and repetition,
including augmented assignment, now use literal folding and guarded immutable
runtime copies. The operations example passes two VICE entries with checksums,
lengths and unchanged saved values verified. Byte slicing now supports runtime
integer bounds, literal None/defaults, negative/clipped indices and forward/reverse
steps. The slicing and sliced binary disk examples pass repeated VICE checks;
Bound find/rfind/index/rindex/count/startswith/endswith now support binary or
integer needles where applicable, optional negative/None bounds, literal folding
and allocation-free runtime searches. CPU/CPython comparisons and repeated VICE
runs verify the search example. Bytes membership in/not in supports subsequences
and integer bytes with Python evaluation order, literal folding and allocation-free
runtime searches; repeated VICE runs verify its results. Remaining methods, bytearray and
slicing of other sequences remain planned.
REU expansion memory now provides source-library store/fetch/swap/verify/fill and
fixed-address DMA on c64-reu/c64-ultimate profiles, with native IRQ-state-preserving
probes/transfers, 24-bit bank/offset addressing and preflight guards. CPU/host
comparisons and repeated VICE runs verify 512 KiB transfers, all 256 banks of a
16 MiB REU, maximum counts/addresses, absence and saved expansion-image bytes.
Raster IRQ callbacks now support static source-function registration, line changes,
critical sections, saved-vector chaining and automatic cleanup. Native backups
preserve shared scratch and reachable frames, including functions used by both
foreground and callbacks. CPU injection at every instruction of a shared function
and repeated VICE runs verify the raster and foreground-float demos. Callback
restrictions and the separate 80-byte stack budget are documented in README.md.
The native asset pipeline now embeds binary files and converts PNG sprite/charset
sheets to immutable bytes, with SPR/CHR preparation, project asset directories,
VIC-compatible placement, map/size reporting and unused literal pruning.
The asset demo passes CPU checks and repeated VICE runs with complete payloads
compared against CPython/Pillow conversion. Tile-map extraction and deduplicated
CHR/MAP preparation now work too. The Python c64.tiles library draws validated
whole maps and cropped viewports; CPU/CPython and repeated VICE runs verify the
48x30 level's glyphs, indices, screen/color RAM and selected font.
Hires bitmap conversion/display now work too (see examples/bitmap). Multicolor
bitmap conversion, larger RSID loaders, higher-level
disk file objects/full response objects, other IRQ sources/asynchronous REU operations and
Ultimate-specific work below remain planned.

## 1. Project Goal

**PyC64** compiles a restricted subset of Python plus PyC64/C64 libraries ahead of time for the Commodore 64.

Use valid Python syntax throughout. Fixed-width integers, arrays and hardware
operations belong to library APIs, with explicit imports. Constants use
`typing.Final`; do not introduce custom keywords. Unsupported Python features
must be diagnosed. Static storage and target limits are compilation restrictions,
documented in `README.md`.

It lets Python developers write real C64 software using familiar syntax:

```python
from c64.video import screen
from c64.input import joystick

x: u8 = 100

screen.clear()

while True:
    if joystick.left:
        x -= 1

    screen.put_char(x, 12, "@")
```

PyC64 compiles directly to native 6502/6510 machine code:

```text
game.py
   ↓
PyC64 compiler
   ↓
typed IR
   ↓
optimizer
   ↓
6502 code generator
   ↓
GAME.PRG
```

Normal PyC64 programs contain **no Python interpreter and no bytecode VM**.

---

# 2. Main Philosophy

PyC64 should be:

- familiar to Python programmers
- specifically designed for the C64
- statically/AOT compiled
- native
- small
- predictable
- suitable for real games and applications
- usable on original C64 hardware
- capable of taking advantage of the C64 Ultimate
- cross-compilable from modern PCs
- eventually compilable directly on a C64 Ultimate

PyC64 is **not CPython**.

It uses Python syntax and familiar concepts while intentionally changing or restricting features that make little sense on a 6502.

---

# 3. Implementation Languages

## Compiler

The PyC64 compiler is written in **portable C**.

Why C:

- GCC and Clang can build it on modern systems.
- cc65 can build C for the C64.
- the compiler can share most of its source across desktop and C64 builds.
- minimal runtime requirements
- direct control over memory
- easy integration with 6502 assembly
- no dependency on a giant host runtime
- much more realistic path toward running the compiler on the C64 Ultimate

The same compiler source should eventually support:

```text
Windows
Linux
macOS
C64 Ultimate
```

The compiler does **not** need to compile PyC64 programs to desktop-native executables.

Every build of `pyc64c` targets the C64.

---

# 4. Compiler Hosting vs Target

Keep these concepts separate.

The compiler may run on:

```text
x86-64 Windows
x86-64 Linux
ARM64 Linux
ARM64 macOS
C64 Ultimate
```

But its output remains:

```text
6502 / 6510 C64 programs
```

So:

```text
HOST                         TARGET

Windows ───────┐
Linux ─────────┤
macOS ─────────┼── pyc64c ───→ C64
C64 Ultimate ──┘
```

There is no need for:

```text
PyC64 → x86-64
PyC64 → ARM64
PyC64 → Windows EXE
PyC64 → Linux ELF
```

PyC64 remains a Python subset targeting the C64.

---

# 5. Compiler Build Strategy

## Desktop

Windows builds default to MSVC with a static C runtime. Clang's MSVC-compatible
driver is supported with `xmake f --toolchain=clang-cl`. Other desktop platforms
can use:

```text
Clang
GCC
```

to build:

```text
pyc64c
pyc64c.exe
```

The desktop build can provide:

- better diagnostics
- faster compilation
- development tools
- emulator launching
- asset conversion
- debugging utilities
- profiling information

---

## C64 Ultimate

Eventually compile the same compiler using:

```text
cc65
```

producing:

```text
PYC64C.PRG
```

Initially the compiler does **not** need to fit or perform well on a stock C64.

Primary compiler hosts:

```text
Desktop          first-class
C64 Ultimate     eventual first-class
Classic C64      optional experiment
```

Programs generated by the compiler can still target classic C64 hardware.

---

# 6. C64 Compiler Constraints

The compiler should be designed from the beginning so that eventually running it on the Ultimate is possible.

Avoid desktop-style compiler architectures that assume huge amounts of memory.

Do not design around:

```text
millions of tiny heap allocations
gigantic ASTs
large dependency graphs
large hash tables everywhere
full source files duplicated several times
huge optimization graphs
```

Prefer compact structures and streaming compilation where practical.

Conceptually:

```text
source
 ↓
lexer
 ↓
parser
 ↓
compact semantic representation
 ↓
small typed IR
 ↓
optimizer
 ↓
6502 output
```

Large programs may eventually be compiled one function/module at a time.

---

# 7. Compiler Platform Layer

Keep operating-system-specific code isolated.

Suggested structure:

```text
compiler/
├── lexer.c
├── parser.c
├── ast.c
├── types.c
├── symbols.c
├── ir.c
├── optimize.c
├── codegen.c
└── compiler.c

platform/
├── desktop.c
└── c64.c
```

The actual language compiler should know as little as possible about the host operating system.

Platform code handles:

```text
opening files
reading source
writing PRGs
directories
terminal output
timing
memory acquisition
```

---

# 8. Target Runtime

The code that actually gets included in PyC64 programs should primarily use:

```text
PyC64
+
6502 assembly
```

Not the compiler's C implementation.

---

# 9. PyC64 Standard Library

Most high-level functionality should be written in **PyC64 itself**.

Example:

```text
stdlib/
├── pyc64/
│   ├── math.py
│   ├── strings.py
│   ├── collections.py
│   ├── random.py
│   └── memory.py
│
└── c64/
    ├── video.py
    ├── sprites.py
    ├── audio.py
    ├── input.py
    └── disk.py
```

This lets PyC64 developers contribute to the standard library without knowing compiler internals.

---

# 10. 6502 Assembly Runtime

Hardware-specific and performance-critical code should use assembly.

```text
runtime/
├── startup.s
├── memory.s
├── strings.s
├── vic2.s
├── sprites.s
├── sid.s
├── cia.s
├── keyboard.s
├── joystick.s
├── irq.s
├── memcpy.s
├── memset.s
└── math.s
```

General rule:

> Write it in PyC64 unless performance, code size, or direct hardware control makes assembly better.

---

# 11. Initial Targets

Support three target profiles.

## `c64`

Original Commodore 64 compatibility.

```text
MOS 6510
64 KB address space
standard VIC-II
standard SID
standard CIA
```

This is the primary software compatibility baseline.

---

## `c64-reu`

C64 with REU-compatible expanded memory.

Implemented: the `c64.reu` Python source library provides synchronous native DMA,
device probing and explicit bank/offset transfers. See README.md for the API,
capacity/address restrictions and 512 KiB/16 MiB examples. Asynchronous operation,
REU-backed ordinary variables and code placement are not implemented.

Programs can explicitly use larger memory spaces while remaining usable on non-Ultimate systems with compatible hardware.

---

## `c64-ultimate`

Allows software to intentionally use C64 Ultimate capabilities.

Possible features include:

```text
Turbo CPU
expanded memory
Ultimate hardware extensions
larger storage
Ultimate-specific APIs
```

Usage:

```bash
pyc64 build game.py --target c64
pyc64 build game.py --target c64-reu
pyc64 build game.py --target c64-ultimate
```

---

# 12. Language Design

PyC64 should remain visually close to Python.

Example:

```python
x = 10

if x > 5:
    print("hello")

while x > 0:
    x -= 1

for i in range(10):
    print(i)
```

Functions:

```python
def add(a: u8, b: u8) -> u8:
    return a + b
```

---

# 13. Initial Language Features

Support:

```text
variables
constants

functions
return values

if
elif
else

while
for
range

imports
modules

integer arithmetic
bitwise operations

strings
arrays
lists

classes/struct-like objects

enums

type annotations
type inference
```

---

# 14. C64-Friendly Primitive Types

Expose exact integer sizes.

```python
health: u8 = 100
velocity: i8 = -4

score: u16 = 12000
offset: i16 = -800
```

Core types:

```text
bool

u8
i8

u16
i16

char

str

bytes

array[T, N]
list[T]
```

Optional later:

```text
u24
i24
u32
i32
```

---

# 15. Type Inference

Do not force Python programmers to annotate everything.

Example:

```python
x = 5
```

can infer a small integer representation.

```python
name = "ASPEN"
```

can infer a static string.

Explicit types remain available when hardware layout matters:

```python
x: u8 = 5
address: u16 = 0xD020
```

---

# 16. Python Features Intentionally Not Supported

Avoid features that would require dragging the full Python runtime model onto a C64.

Initially exclude:

```text
eval()
exec()

metaclasses

runtime monkey patching

dynamic attribute creation

arbitrary reflection

full CPython introspection

arbitrary-precision integers by default

dynamic module loading

CPython extensions
```

Compiler errors should clearly explain unsupported functionality.

---

# 17. Classes

PyC64 classes should compile much closer to structs than CPython objects.

Example:

```python
class Player:
    x: u8
    y: u8
    health: u8

    def damage(self, amount: u8):
        self.health -= amount
```

Possible layout:

```text
Player

offset 0: x
offset 1: y
offset 2: health
```

Methods compile as normal native functions receiving a reference to the object.

No Python dictionary needs to exist for every object.

---

# 18. Memory Model

Memory usage should be visible and predictable.

Prefer:

```text
static allocation
stack allocation
fixed arrays
compile-time-known data
simple arenas
explicit dynamic containers
```

Example:

```python
from pyc64.types import u8, array

buffer: array[u8, 256] = array(u8, 256)
```

means exactly what it looks like: reserve 256 bytes.

Dynamic allocation can exist, but should not be the default solution for everything.

---

# 19. Imports

Imports happen during compilation.

Example:

```python
from c64.video import screen
```

means:

```text
find module
 ↓
compile required code
 ↓
resolve dependencies
 ↓
remove unused functions
 ↓
link required pieces
```

There is normally no runtime Python import system.

---

# 20. Library Structure

## Generic PyC64

```text
pyc64.math
pyc64.strings
pyc64.collections
pyc64.memory
pyc64.random
pyc64.io
```

## C64

```text
c64.video
c64.sprite
c64.audio
c64.sid
c64.cia
c64.keyboard
c64.joystick
c64.disk
c64.memory
c64.irq
c64.kernal
```

## Ultimate

```text
c64ultimate.memory
c64ultimate.turbo
c64ultimate.storage
c64ultimate.hardware
```

---

# 21. Target Validation

If someone writes:

```python
from c64ultimate import turbo
```

and builds:

```bash
pyc64 build --target c64
```

the compiler should produce something like:

```text
error: module 'c64ultimate.turbo'
requires target 'c64-ultimate'
```

---

# 22. Example Hardware API

## Video

```python
from c64.video import screen

screen.clear()
screen.border = 6
screen.background = 0

screen.put_char(10, 10, "A")
```

## Sprites

```python
from c64.sprite import Sprite

player = Sprite(0)

player.enable()
player.x = 100
player.y = 80
player.color = 1
```

## Input

```python
from c64.input import joystick

if joystick.port2.left:
    player.x -= 1
```

## SID

```python
from c64.audio import sid

voice = sid.voice(0)

voice.frequency = 440
voice.gate = True
```

---

# 23. Compiler Pipeline

```text
.py source
    ↓
lexer
    ↓
parser
    ↓
AST
    ↓
semantic analysis
    ↓
type inference
    ↓
HIR
    ↓
compact C64 IR
    ↓
optimizer
    ↓
6502 backend
    ↓
linker
    ↓
.PRG
```

Do not translate Python to C first.

PyC64 should have its own backend and understand the C64 directly.

---

# 24. Intermediate Representation

Example:

```python
x: u8 = 5
x += 2
```

could become:

```text
const.u8 5
store x

load.u8 x
add.u8 2
store x
```

Then codegen:

```asm
lda #5
sta x

lda x
clc
adc #2
sta x
```

Having an IR allows optimizations without tying the parser directly to 6502 instructions.

---

# 25. 6502-Specific Optimizations

PyC64 should optimize specifically for the machine.

Example:

```python
x += 1
```

can become:

```asm
inc x
```

when valid.

Constant folding:

```python
x = 20 * 4
```

becomes:

```python
x = 80
```

before code generation.

---

# 26. Whole-Program Optimization

Because PyC64 is AOT compiled, use whole-program information aggressively.

Remove:

```text
unused functions
unused imports
unused classes
unused constants
unreachable branches
unused runtime functions
```

A large standard library should not imply a large executable.

---

# 27. Specialized Runtime Calls

High-level operations can map directly to optimized native routines.

Example:

```python
screen.clear()
```

might become:

```asm
jsr __pyc64_screen_clear
```

Likewise:

```python
memory.copy(source, destination, 256)
```

can use a hand-optimized assembly function instead of generating hundreds of generic operations.

---

# 28. PRG Generation

The compiler should directly produce a normal C64 `.PRG`.

```bash
pyc64 build hello.py
```

Output:

```text
build/
└── HELLO.PRG
```

The build system handles:

```text
load address
startup code
optional BASIC SYS launcher
runtime routines
native code
static data
assets
```

---

# 29. Desktop CLI

Desktop `pyc64` tooling can wrap the compiler.
The desktop `pyc64c` executable now provides these commands directly, without a
Python wrapper; `new NAME` is also implemented.

```bash
pyc64 build
pyc64 run
pyc64 check
pyc64 clean
pyc64 size
pyc64 asm
```

---

# 30. `pyc64 run`

On desktop:

```bash
pyc64 run game.py
```

does:

```text
compile
 ↓
GAME.PRG
 ↓
launch VICE
 ↓
execute GAME.PRG
```

So developers get a quick modern workflow without requiring a desktop-native PyC64 backend.

---

# 31. C64 Ultimate Compiler Interface

The Ultimate version can use a simpler interface.

For example:

```text
PYC64C "GAME.PY"
```

or eventually a shell:

```text
> pyc64c game.py

PyC64 Compiler
Compiling GAME.PY...

Code:    8120 bytes
Data:    2110 bytes

Wrote GAME.PRG
```

Then:

```text
> run game.prg
```

This provides real on-device development without requiring PyC64 to self-host.

---

# 32. Project Files

Desktop projects use `pyc64.toml`. The documented string-valued settings in
README.md are implemented, including assets.directory; unsupported
settings produce diagnostics.

```text
pyc64.toml
```

Example:

```toml
[project]
name = "space-game"
entry = "main.py"

[target]
machine = "c64"

[build]
optimize = "size"

[assets]
directory = "assets"
```

Then:

```bash
pyc64 build
```

---

# 33. C64-Side Simplicity

The C64 Ultimate compiler should not need the full desktop project system immediately.

It may initially compile:

```text
MAIN.PY
UTIL.PY
GAME.PY
```

using simple filenames and directories.

The sophisticated desktop CLI can remain a host convenience rather than something the C64 compiler must duplicate.

---

# 34. Asset Pipeline

Desktop tools can provide image/audio conversion.

Implemented: native PNG sprite/charset, tile-map and hires bitmap conversion, binary embedding, SPR/CHR/MAP/HBM/SCR
preparation, compile-time Python asset APIs and aligned VIC bank-0 linking.
See README.md, examples/assets, examples/tilemap and examples/bitmap.
VBI/CIA-timed PSID music loading/playback now work (examples/music and examples/music_cia),
including custom timer A periods, independent raster callbacks and timer B KERNAL service.
Returning-init RSID v2..4 with native KERNAL-mapped CIA1/VIC IRQ handlers now work
(examples/music_rsid), including exclusive ownership, private zero page and hardware cleanup.
Standalone full-control RSID PRG launch now covers BASIC, busy-loop/non-returning init,
CIA2/NMI, banked/direct vectors and declared relocation ranges (examples/music_standalone),
with original-RAM copying and stock ROM initialization. The one-PRG staging/free-page
bounds remain explicit. Multicolor bitmap conversion, larger RSID loaders and multi-SID
playback remain planned. The C64-hosted compiler remains planned;
the same asset APIs accept prepacked SPR/CHR/MAP/HBM/SCR files without needing PNG decoding.

Example:

```text
assets/
├── player.png
├── charset.png
├── level.png
└── music.sid
```

Build tools convert these into C64-ready representations.

The C64-hosted compiler does not initially need to perform PNG decoding or other expensive asset processing.

Instead, converted assets can already be present:

```text
PLAYER.SPR
CHARSET.CHR
LEVEL.MAP
MUSIC.SID
```

This keeps `PYC64C.PRG` much smaller.

---

# 35. Compiler Feature Differences

The actual **language** should be the same everywhere.

But host tooling does not need to be identical.

Desktop:

```text
compiler
asset pipeline
VICE launcher
pretty diagnostics
project management
debug tooling
```

C64 Ultimate:

```text
compiler
linker
basic diagnostics
PRG output
```

Both should compile the same valid PyC64 source.

---

# 36. Inline Assembly

Eventually allow advanced developers to escape into 6502 ASM.

Example:

```python
from c64 import asm

asm("""
    lda #$06
    sta $d020
""")
```

This is useful for:

```text
cycle-sensitive code
IRQ handlers
raster tricks
hardware experiments
extreme size optimization
```

---

# 37. External Assembly

Allow linking dedicated assembly files:

```text
main.py
fast_render.s
music.s
```

Declare external functions in a Python stub module, for example `fast_render.pyi`:

```python
def render() -> None: ...
```

The program imports and calls `render()` using ordinary Python syntax. The
planned linker connects that declaration to the assembly implementation; no
`extern` keyword is added to the language.

---

# 38. C Interoperability

Later, allow existing C64 C code to participate.

For example:

```text
existing cc65 library
        ↓
object code
        ↓
PyC64 linker
        ↓
GAME.PRG
```

This would make it possible to reuse existing C64 libraries rather than recreating everything.

---

# 39. Rust for C64

Rust remains a separate future project.

Eventually:

```text
Desktop Rust
     ↓
LLVM-MOS / suitable backend
     ↓
native C64
```

PyC64 does not depend on Rust.

But Rust, C, PyC64, and assembly could eventually share:

```text
ABI conventions
hardware definitions
asset formats
linker conventions
runtime routines
```

---

# 40. Compiler Development Order

Start tiny.

First compile:

```python
x: u8 = 5
```

Then:

```python
x: u8 = 5
x += 1
```

Then:

```python
while True:
    x += 1
```

Then:

```python
def add(a: u8, b: u8) -> u8:
    return a + b
```

Then:

```python
from c64.video import screen

screen.border = 6
```

Do not start by implementing all of Python.

---

# 41. First Usable Language

Initial useful feature set:

```text
u8
i8
u16
i16
bool

variables
constants

functions

if
elif
else

while

for/range

arithmetic
comparisons
bitwise operations

arrays

basic strings

imports

C64 hardware modules
```

That is already enough to create significant software.

---

# 42. First Real Demo

The first proper test should be something like:

```python
from c64.video import screen
from c64.input import joystick

x: u8 = 20
y: u8 = 12

while True:
    if joystick.left:
        x -= 1

    if joystick.right:
        x += 1

    if joystick.up:
        y -= 1

    if joystick.down:
        y += 1

    screen.clear()
    screen.put_char(x, y, "@")
```

The result should be:

```text
native GAME.PRG
```

running correctly in VICE and later on actual C64 hardware.

---

# 43. Features to Delay

Do not initially implement:

```text
full Python object semantics

async

threads

decorators

metaclasses

generators

full exception system

dynamic imports

reflection

eval

pip compatibility

CPython modules

complex garbage collection
```

Add features only when they provide enough value to justify their cost on a C64.

---

# 44. Diagnostics

Compiler errors should be much nicer than typical retro development tools.

Example:

```text
error: value 300 does not fit in u8

 --> game.py:14:9
  |
14| x: u8 = 300
  |         ^^^
  |
help: use u16 instead
```

Desktop and Ultimate builds can use different presentation styles while reporting the same underlying error.

---

# 45. Size Reporting

Because C64 memory is so constrained:

```bash
pyc64 size game.py
```

should eventually report something like:

```text
Program layout

Code       7,842 bytes
Data       2,108 bytes
Runtime    1,220 bytes
Assets     8,192 bytes

Total     19,362 bytes
```

It could eventually also show memory regions.

---

# 46. Testing

Compiler components should have desktop tests for:

```text
lexer
parser
type checker
IR
optimizer
code generator
linker
```

Generated binaries should eventually be tested automatically in an emulator.

```text
PyC64 source
     ↓
compile
     ↓
launch headless emulator
     ↓
inspect memory/output
     ↓
verify result
```

---

# 47. Repository Layout

Initial layout:

```text
pyc64/
├── xmake.lua
├── README.md
│
├── compiler/
│   ├── lexer.c
│   ├── lexer.h
│   ├── parser.c
│   ├── parser.h
│   ├── ast.c
│   ├── ast.h
│   ├── types.c
│   ├── types.h
│   ├── symbols.c
│   ├── symbols.h
│   ├── ir.c
│   ├── ir.h
│   ├── optimize.c
│   ├── optimize.h
│   ├── codegen.c
│   └── codegen.h
│
├── platform/
│   ├── desktop.c
│   └── c64.c
│
├── runtime/
│   └── 6502/
│
├── stdlib/
│   ├── pyc64/
│   ├── c64/
│   └── c64ultimate/
│
├── examples/
│   ├── hello/
│   ├── joystick/
│   ├── sprites/
│   └── sid/
│
└── tests/
```

Start as one executable rather than prematurely breaking everything into libraries.

---

# 48. Build Systems

Desktop:

```text
xmake
```

or a similarly simple cross-platform C build system.

C64:

```text
cc65 / cl65
```

The core source should avoid compiler-specific extensions where practical.

---

# 49. Ultimate-First Native Compiler

Do not waste early development effort trying to make the compiler comfortable on a stock C64.

Development progression:

```text
Desktop compiler works
        ↓
Language becomes usable
        ↓
Compiler architecture stabilized
        ↓
Build compiler with cc65
        ↓
Run compiler on C64 Ultimate
        ↓
Reduce memory use
        ↓
Improve Ultimate experience
        ↓
Experiment with classic C64 compiler hosting
```

Classic-hosted compilation is optional.

---

# 50. Long-Term Development Experience

Desktop:

```bash
pyc64 new mygame
cd mygame
pyc64 run
```

C64 Ultimate:

```text
EDIT "GAME.PY"

PYC64C "GAME.PY"

RUN "GAME.PRG"
```

Both environments use the same source language and generate the same kind of native software.

---

# 51. Long-Term Ecosystem

Eventually:

```text
                        C64 Development
                              │
             ┌────────────────┼────────────────┐
             │                │                │
           PyC64              C              Rust
             │                │                │
       Python subset       traditional      modern
       high-level           systems         systems
             │                │                │
             └────────────────┼────────────────┘
                              │
                         6502 / 6510
                              │
                             C64
```

Assembly remains available underneath everything for maximum control.

---

# 52. Final Architecture

```text
                    PyC64 source
                         │
                         ▼
                 pyc64c compiler
                    written in C
                         │
              ┌──────────┴──────────┐
              │                     │
       desktop compiler       Ultimate compiler
        GCC / Clang               cc65
              │                     │
              └──────────┬──────────┘
                         │
                  semantic analysis
                         │
                    typed C64 IR
                         │
                     optimizer
                         │
                 6502/6510 backend
                         │
          ┌──────────────┴──────────────┐
          │                             │
   PyC64 standard library        6502 ASM runtime
          │                             │
          └──────────────┬──────────────┘
                         ▼
                       .PRG
                         │
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
     Classic C64       C64 + REU    C64 Ultimate
```

# Project Identity

PyC64 can be summarized as:

> **Write C64 programs like Python. Compile them like a native language. Run them directly on the 6502.**

And the compiler itself follows the same retro-friendly philosophy:

> **One portable C compiler codebase that runs on modern PCs today and eventually on the C64 Ultimate itself.**

from pyc64.types import u8
from c64.video import screen
import c64.disk as disk

# Use a writable disk with free PYC64-DEMO and PYC64-RENAMED filenames.
screen.clear()
status_open: bool = disk.open(15, 8, 15, "")
created: bool = False
renamed: u8 = disk.DOS_UNAVAILABLE
missing: u8 = disk.DOS_UNAVAILABLE
value: u8 = 0
read_ok: bool = False
if status_open:
    startup: u8 = disk.dos_code(15)
    opened: bool = disk.open(2, 8, 2, "pyc64-demo,s,w")
    dos: u8 = disk.dos_code(15)
    if opened and dos == disk.DOS_OK:
        created = disk.write_text(2, "hello")
    if opened:
        disk.close(2)
        closed: u8 = disk.dos_code(15)
        if closed != disk.DOS_OK:
            created = False
    if created:
        renamed = disk.command(15, "r0:pyc64-renamed=pyc64-demo")
        opened = disk.open(2, 8, 2, "pyc64-demo,s,r")
        missing = disk.dos_code(15)
        if opened:
            disk.close(2)
        opened = disk.open(2, 8, 2, "pyc64-renamed,s,r")
        dos = disk.dos_code(15)
        if opened and dos == disk.DOS_OK:
            value = disk.read_byte(2)
            read_ok = not disk.failed() and value == 72
        if opened:
            disk.close(2)
    disk.close(15)
print("DOS rename:", renamed, "missing:", missing)
print("Text:", created, read_ok, value)

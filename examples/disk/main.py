from pyc64.types import u8, u16
from c64.video import screen
import c64.disk as disk

# Attach a writable disk to device 8. This demo creates PYC64-DEMO.
screen.clear()
status_open: bool = disk.open(15, 8, 15, "")
startup: u8 = disk.DOS_UNAVAILABLE
dos: u8 = disk.DOS_UNAVAILABLE
if status_open:
    startup = disk.dos_code(15)
opened: bool = False
written: bool = False
if status_open and startup != disk.DOS_UNAVAILABLE:
    opened = disk.open(2, 8, 2, "pyc64-demo,s,w")
    # OPEN's KERNAL result alone does not detect FILE EXISTS or DISK FULL.
    if opened:
        dos = disk.dos_code(15)
if opened and dos == disk.DOS_OK:
    written = disk.write_byte(2, 0)
    if written:
        written = disk.write_byte(2, 255)
    if written:
        written = disk.write_byte(2, 42)
if opened:
    disk.close(2)
    if disk.failed():
        written = False
    closed_code: u8 = disk.dos_code(15)
    if dos == disk.DOS_OK:
        dos = closed_code
    if closed_code != disk.DOS_OK:
        written = False

count: u16 = 0
total: u16 = 0
read_ok: bool = False
last: u8 = 0
if written:
    opened = disk.open(2, 8, 2, "pyc64-demo,s,r")
    dos = disk.dos_code(15)
    if opened and dos == disk.DOS_OK:
        # Bound the loop even if a drive misbehaves; process the EOI byte.
        for index in range(3):
            last = disk.read_byte(2)
            if disk.failed():
                break
            total += last
            count += 1
            if disk.eof():
                read_ok = count == 3 and total == 297
                break
    if opened:
        disk.close(2)
        if disk.failed():
            read_ok = False
        dos = disk.dos_code(15)
        if dos != disk.DOS_OK:
            read_ok = False
if status_open:
    # Closing command channel 15 closes the drive's files; close it last.
    disk.close(15)
print("Disk:", written, read_ok)
print("Bytes:", count, "sum:", total)
print("DOS:", dos)

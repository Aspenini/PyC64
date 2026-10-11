from pyc64.types import u8, u16, array
from c64.video import screen
import c64.disk as disk

# Zero budgets leave the buffer untouched and perform no byte operations.
# 1541 DOS itself inserts a CR when a new SEQ file closes without data writes.

screen.clear()
buffer = array(u8, 1)
buffer[0] = 99
written: u16 = 0
received: u16 = 0
write_ok: bool = False
read_ok: bool = False
dos: u8 = disk.DOS_UNAVAILABLE
status_open: bool = disk.open(15, 8, 15, "")
if status_open:
    startup: u8 = disk.dos_code(15)
    if startup != disk.DOS_UNAVAILABLE:
        opened: bool = disk.open(2, 8, 2, "pyc64-zero,s,w")
        if opened:
            dos = disk.dos_code(15)
            if dos == disk.DOS_OK:
                written = disk.write_from(2, buffer, 0)
                write_ok = written == 0 and not disk.failed()
            disk.close(2)
            if disk.failed():
                write_ok = False
            closed: u8 = disk.dos_code(15)
            if dos == disk.DOS_OK:
                dos = closed
            if closed != disk.DOS_OK:
                write_ok = False
    if write_ok:
        opened = disk.open(2, 8, 2, "pyc64-zero,s,r")
        if opened:
            dos = disk.dos_code(15)
            if dos == disk.DOS_OK:
                received = disk.read_into(2, buffer, 0)
                read_ok = received == 0 and buffer[0] == 99 and not disk.failed()
            disk.close(2)
            if disk.failed():
                read_ok = False
            dos = disk.dos_code(15)
            if dos != disk.DOS_OK:
                read_ok = False
    disk.close(15)
print("Zero count:", written, received, write_ok, read_ok, buffer[0])
print("DOS:", dos)

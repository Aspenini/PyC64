from pyc64.types import u8, u16, array
from c64.video import screen
import c64.disk as disk

# Attach a writable device-8 disk with a free PYC64-BUFFER filename.
screen.clear()
source = [0, 255, 42]
destination = array(u8, 8)
status_open: bool = disk.open(15, 8, 15, "")
dos: u8 = disk.DOS_UNAVAILABLE
written: u16 = 0
received: u16 = 0
write_ok: bool = False
read_ok: bool = False
if status_open:
    dos = disk.dos_code(15)
    if dos != disk.DOS_UNAVAILABLE:
        opened: bool = disk.open(2, 8, 2, "pyc64-buffer,s,w")
        if opened:
            dos = disk.dos_code(15)
            if dos == disk.DOS_OK:
                written = disk.write_from(2, source, 3)
                write_ok = written == 3 and not disk.failed()
            disk.close(2)
            if disk.failed():
                write_ok = False
            closed_code: u8 = disk.dos_code(15)
            if dos == disk.DOS_OK:
                dos = closed_code
            if closed_code != disk.DOS_OK:
                write_ok = False
    if write_ok:
        opened = disk.open(2, 8, 2, "pyc64-buffer,s,r")
        if opened:
            dos = disk.dos_code(15)
            if dos == disk.DOS_OK:
                received = disk.read_into(2, destination, 3)
                read_ok = received == 3 and disk.eof() and not disk.failed()
            disk.close(2)
            if disk.failed():
                read_ok = False
            dos = disk.dos_code(15)
            if dos != disk.DOS_OK:
                read_ok = False
    disk.close(15)
print("Buffers:", written, received, write_ok, read_ok)
print("Bytes:", destination[0], destination[1], destination[2], destination[7])
print("DOS:", dos)

from pyc64.types import u8
import c64.disk as disk

# VICE's default configuration has no device 9.
opened: bool = disk.open(2, 9, 2, "absent,s,r")
open_error: u8 = disk.error()
open_status: u8 = disk.status()
open_failed: bool = disk.failed()
disk.close(2)
missing: u8 = disk.read_byte(7)
read_error: u8 = disk.error()
written: bool = disk.write_byte(7, 255)
write_error: u8 = disk.error()
print("Disk errors:", opened, open_error, open_status, open_failed)
print("Closed:", missing, read_error, written, write_error)

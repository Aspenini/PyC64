from typing import Final
from pyc64.types import u8, u16
from c64.kernal import open, close, read_byte, write_byte, write_text, status, error

# Serial-bus READST bits. EOF accompanies the final byte of a nonempty file.
WRITE_TIMEOUT: Final[u8] = 1
READ_TIMEOUT: Final[u8] = 2
EOF: Final[u8] = 64
DEVICE_NOT_PRESENT: Final[u8] = 128

# Drive DOS codes are separate from KERNAL errors and serial status bits.
DOS_OK: Final[u8] = 0
DOS_FILES_SCRATCHED: Final[u8] = 1
DOS_FILE_NOT_FOUND: Final[u8] = 62
DOS_FILE_EXISTS: Final[u8] = 63
DOS_DISK_FULL: Final[u8] = 72
DOS_POWER_UP: Final[u8] = 73
DOS_UNAVAILABLE: Final[u8] = 255


def eof() -> bool:
    return (status() & EOF) != 0


def failed() -> bool:
    # EOI alone is normal; the other status bits or a KERNAL error are failures.
    return error() != 0 or (status() & 191) != 0


def dos_code(logical: u8) -> u8:
    # The caller owns an open secondary-address-15 command/status channel.
    # Consume NN,MESSAGE,TT,SS<CR>, including the final EOI byte. A malformed
    # record is drained to CR/EOI within the same 64-byte bound.
    code: u8 = 0
    field: u8 = 0
    digits: u8 = 0
    message: bool = False
    valid: bool = True
    for index in range(64):
        value: u8 = read_byte(logical)
        if failed():
            return DOS_UNAVAILABLE
        if value == 13:
            if valid and field == 3 and digits == 2:
                return code
            return DOS_UNAVAILABLE
        if field == 1:
            if value == 44:
                if not message:
                    valid = False
                field = 2
                digits = 0
            elif value < 32:
                valid = False
            else:
                message = True
        elif value >= 48 and value <= 57 and digits < 2:
            if field == 0:
                code = code * 10 + value - 48
            digits += 1
        elif value == 44 and digits == 2 and field < 3:
            field += 1
            digits = 0
        else:
            valid = False
        if eof():
            return DOS_UNAVAILABLE
    return DOS_UNAVAILABLE


def command(logical: u8, text: str) -> u8:
    # write_text sends one complete command and flushes it with UNLISTEN.
    if not write_text(logical, text):
        return DOS_UNAVAILABLE
    return dos_code(logical)


def read_into(logical: u8, buffer, count: u16) -> u16:
    # The caller owns the channel and supplies a byte budget. For a known empty
    # file, pass zero; do not probe the stream with CHRIN. Process the final EOI
    # byte before returning and leave the unused buffer tail unchanged.
    limit: u16 = len(buffer)
    if count < limit:
        limit = count
    received: u16 = 0
    for index in range(limit):
        value: u8 = read_byte(logical)
        if failed():
            break
        buffer[index] = value
        received += 1
        if eof():
            break
    return received


def write_from(logical: u8, buffer, count: u16) -> u16:
    # Count only successful, flushed byte operations. A failed serial write may
    # still have sent its byte; callers must inspect transport and DOS status.
    limit: u16 = len(buffer)
    if count < limit:
        limit = count
    written: u16 = 0
    for index in range(limit):
        value = buffer[index]
        if value < 0 or value > 255:
            break
        if not write_byte(logical, u8(value)):
            break
        written += 1
    return written

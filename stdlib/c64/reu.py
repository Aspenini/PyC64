from typing import Final
from pyc64.types import u8, u16
from c64._reu import present, transfer as _transfer

TO_REU: Final[u8] = 0
FROM_REU: Final[u8] = 1
SWAP: Final[u8] = 2
VERIFY: Final[u8] = 3
FIX_REU: Final[u8] = 64
FIX_C64: Final[u8] = 128


def transfer(c64_address: u16, reu_address: u16, bank: u8, count: u16,
             operation: u8, control: u8) -> bool:
    # Bank plus offset forms a 24-bit REU address. Physical capacity is caller-owned.
    # Count zero is a no-op, never the hardware's special 65536-byte transfer.
    if c64_address < 0 or c64_address > 65535 or reu_address < 0 or reu_address > 65535:
        return False
    if bank < 0 or bank > 255 or count < 0 or count > 65535:
        return False
    if operation < 0 or operation > 3 or control < 0 or control > 255 or (control & 63) != 0:
        return False
    if count == 0:
        return True
    if (control & FIX_C64) == 0 and c64_address > 65535 - count + 1:
        return False
    if (control & FIX_REU) == 0 and bank == 255 and reu_address > 65535 - count + 1:
        return False
    if not present():
        return False
    # END OF BLOCK without VERIFY ERROR. Read status exactly once in the primitive.
    return (_transfer(c64_address, reu_address, bank, count, operation, control) & 96) == 64


def store(c64_address: u16, reu_address: u16, bank: u8, count: u16) -> bool:
    return transfer(c64_address, reu_address, bank, count, TO_REU, 0)


def fetch(c64_address: u16, reu_address: u16, bank: u8, count: u16) -> bool:
    return transfer(c64_address, reu_address, bank, count, FROM_REU, 0)


def swap(c64_address: u16, reu_address: u16, bank: u8, count: u16) -> bool:
    return transfer(c64_address, reu_address, bank, count, SWAP, 0)


def verify(c64_address: u16, reu_address: u16, bank: u8, count: u16) -> bool:
    return transfer(c64_address, reu_address, bank, count, VERIFY, 0)


def fill(c64_address: u16, reu_address: u16, bank: u8, count: u16) -> bool:
    # Repeat the single C64 source byte over an REU span.
    return transfer(c64_address, reu_address, bank, count, TO_REU, FIX_C64)

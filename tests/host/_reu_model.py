"""A 1764/1750-style REU register and DMA model for host-side library tests.

Reads and writes of $DF00-$DF0A go to the REU registers; a write to the
command register with EXECUTE and FF00 disabled performs the transfer.
"""


class REUMemory(list):
    def __init__(self, memory, size=512 * 1024, attached=True):
        super().__init__(memory)
        self.expansion = bytearray(size)
        self.attached = attached
        self.registers = [0] * 11
        self.registers[0] = 0x10 if size > 128 * 1024 else 0
        self.registers[1] = 0x10
        self.reads = []
        self.writes = []
        self.transfers = []

    def __getitem__(self, key):
        if isinstance(key, int) and 0xDF00 <= key <= 0xDF0A:
            self.reads.append(key)
            if not self.attached:
                return 255
            index = key - 0xDF00
            value = self.registers[index]
            if index == 0:
                self.registers[0] &= 0x1F
            elif index == 6:
                value |= 0xF8  # Upper bank latch bits cannot be read back.
            elif index == 9:
                value |= 0x1F
            elif index == 10:
                value |= 0x3F
            return value
        return super().__getitem__(key)

    def __setitem__(self, key, value):
        if isinstance(key, int) and 0xDF00 <= key <= 0xDF0A:
            self.writes.append((key, value))
            if not self.attached or key == 0xDF00:
                return
            self.registers[key - 0xDF00] = value
            if key == 0xDF01 and value & 0x90 == 0x90:
                self.dma()
            return
        super().__setitem__(key, value)

    def dma(self):
        r = self.registers
        c64 = r[2] | (r[3] << 8)
        expansion = r[4] | (r[5] << 8) | (r[6] << 16)
        count = (r[7] | (r[8] << 8)) or 65536
        mode, control = r[1] & 3, r[10]
        self.transfers.append((c64, expansion, count, mode, control))
        failed = False
        for index in range(count):
            cbyte = super().__getitem__(c64)
            eaddress = expansion % len(self.expansion)
            ebyte = self.expansion[eaddress]
            if mode == 0:
                self.expansion[eaddress] = cbyte
            elif mode == 1:
                super().__setitem__(c64, ebyte)
            elif mode == 2:
                super().__setitem__(c64, ebyte)
                self.expansion[eaddress] = cbyte
            elif cbyte != ebyte:
                failed = True
                break
            if not control & 0x80:
                c64 = (c64 + 1) & 65535
            if not control & 0x40:
                expansion = (expansion + 1) & 0xFFFFFF
        r[0] |= 0x20 if failed else 0x40
        r[1] &= 0x7F
        r[2], r[3] = c64 & 255, c64 >> 8
        r[4], r[5], r[6] = expansion & 255, (expansion >> 8) & 255, expansion >> 16
        r[7], r[8] = 1, 0

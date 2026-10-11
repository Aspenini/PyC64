"""Register-level hardware adapters for CPU/host comparisons; VICE checks real DMA."""


class IRQMemory(list):
    """VIC W1C flags and KERNAL-compatible IRQ entry; timing is checked in VICE."""
    def __init__(self, program, period=500, cia_period=0):
        super().__init__(program.cpu.memory)
        self.cpu = program.cpu
        self.period = period
        self.cia_period = cia_period
        self.clock = self.delivered = self.chained = 0
        self.pending = self.mask = self.line = 0
        self.beam = 37
        self.cia_pending = False
        self.writes = []
        # Standard KERNAL entry saves A/X/Y and dispatches IRQ versus BRK.
        self[0xFF48:0xFF5B] = bytes.fromhex('48 8a 48 98 48 ba bd 04 01 29 10 f0 03 6c 16 03 6c 14 03')
        self[0xFFFE:0x10000] = [0x48, 0xFF]
        self[0x0314:0x0316] = [0x31, 0xEA]
        # A small old handler consumes CIA status and uses the KERNAL epilogue.
        self[0xEA31:0xEA3A] = bytes.fromhex('ad 0d dc 68 a8 68 aa 68 40')
        self[0xEA81:0xEA87] = bytes.fromhex('68 a8 68 aa 68 40')
        super().__setitem__(0xD011, 0x1B)
        program.cpu.memory = self
        program.irq = self

    def __getitem__(self, key):
        if key == 0xD019:
            return 0x70 | self.pending | (0x80 if self.pending & self.mask else 0)
        if key == 0xD01A:
            return 0xF0 | self.mask
        if key == 0xD012:
            return self.beam & 255
        if key == 0xD011:
            return (super().__getitem__(key) & 127) | (0x80 if self.beam > 255 else 0)
        if key == 0xDC0D:
            self.chained += 1
            result = 0x81 if self.cia_pending else 0
            self.cia_pending = False
            return result
        return super().__getitem__(key)

    def __setitem__(self, key, value):
        if isinstance(key, int) and key in (0xD019, 0xD01A, 0xD011, 0xD012, 0x0314, 0x0315):
            self.writes.append((key, value, bool(self.cpu.p & self.cpu.INTERRUPT)))
            if key == 0xD019:
                self.pending &= ~value
                return
            if key == 0xD01A:
                self.mask = value & 15
            elif key == 0xD011:
                self.line = (self.line & 255) | (256 if value & 128 else 0)
            elif key == 0xD012:
                self.line = (self.line & 256) | value
        super().__setitem__(key, value)

    def handle(self, program):
        self.clock += 1
        if self.period and self.clock % self.period == 0:
            self.pending |= 1
        if self.cia_period and self.clock % self.cia_period == 0:
            self.cia_pending = True
        if not self.cpu.p & self.cpu.INTERRUPT and (self.pending & self.mask or self.cia_pending):
            self.cpu.irq()
            self.delivered += 1
            return True
        return False


class CIAMemory(IRQMemory):
    """Cycle-driven CIA1 latch/counter, control and read-to-clear ICR adapter.

    Continuous PHI2 timers suffice for this profile; unsupported modes are used
    only for ownership rejection checks. VICE validates full chip behavior.
    """
    def __init__(self, program, pal=True, raster_cycles=19656):
        self.latches = [0x4025 if pal else 0x4295, 0xFFFF]
        self.counters = self.latches.copy()
        self.controls = [1, 8]
        self.cia_flags, self.cia_mask = 0, 1
        self.underflows = [0, 0]
        self.timer_writes = []
        self.icr_reads = 0
        self.kernal_calls = 0
        self.last_cycle = program.cpu.processorCycles
        self.raster_cycles = raster_cycles
        self.next_raster = self.last_cycle + raster_cycles if raster_cycles else 0
        super().__init__(program, period=0)
        self[0x02A6] = int(pal)
        self[0xEA31:0xEA40] = bytes.fromhex('e6 a2 d0 02 e6 a1 ad 0d dc 68 a8 68 aa 68 40')

    def __getitem__(self, key):
        if isinstance(key, int) and 0xDC04 <= key <= 0xDC07:
            return (self.counters[(key - 0xDC04) // 2] >> (8 * (key & 1))) & 255
        if key in (0xDC0E, 0xDC0F):
            return self.controls[key - 0xDC0E]
        if key == 0xDC0D:
            self.icr_reads += 1
            result = self.cia_flags | (0x80 if self.cia_flags & self.cia_mask else 0)
            self.cia_flags = 0
            return result
        return super().__getitem__(key)

    def __setitem__(self, key, value):
        if isinstance(key, int) and key in (0xDC04, 0xDC05, 0xDC06, 0xDC07, 0xDC0D, 0xDC0E, 0xDC0F):
            self.timer_writes.append((key, value, bool(self.cpu.p & self.cpu.INTERRUPT)))
            if key == 0xDC0D:
                if value & 0x80:
                    self.cia_mask |= value & 0x1F
                else:
                    self.cia_mask &= ~(value & 0x1F)
            elif key >= 0xDC0E:
                index = key - 0xDC0E
                self.controls[index] = value & ~0x10
                if value & 0x10:
                    self.counters[index] = self.latches[index]
            else:
                index, high = (key - 0xDC04) // 2, key & 1
                if high:
                    self.latches[index] = (self.latches[index] & 255) | (value << 8)
                    if not self.controls[index] & 1:
                        self.counters[index] = self.latches[index]
                else:
                    self.latches[index] = (self.latches[index] & 0xFF00) | value
            return
        super().__setitem__(key, value)

    def handle(self, program):
        now = self.cpu.processorCycles
        elapsed, self.last_cycle = now - self.last_cycle, now
        for index in range(2):
            if self.controls[index] & 1:
                left = self.counters[index] - elapsed
                while left < 0:
                    self.cia_flags |= 1 << index
                    self.underflows[index] += 1
                    left += self.latches[index] + 1
                    if self.controls[index] & 8:
                        self.controls[index] &= ~1
                        break
                self.counters[index] = max(0, left)
        if self.raster_cycles and now >= self.next_raster:
            self.pending |= 1
            self.next_raster += self.raster_cycles
        if self.cpu.pc == 0xEA31:
            self.kernal_calls += 1
        if not self.cpu.p & self.cpu.INTERRUPT and (self.pending & self.mask or self.cia_flags & self.cia_mask):
            self.cpu.irq()
            self.delivered += 1
            return True
        return False


class SidLaunchMemory(list):
    """RAM under I/O survives writes to mapped hardware; no ROM stubs in copy tests."""
    def __init__(self, memory):
        super().__init__(memory)
        self.io = {}

    def mapped(self, key):
        port = super().__getitem__(1)
        return isinstance(key,int) and 0xD000 <= key < 0xE000 and port & 3 and port & 4

    def __getitem__(self,key):
        if self.mapped(key): return self.io.get(key,0)
        return super().__getitem__(key)

    def __setitem__(self,key,value):
        if self.mapped(key): self.io[key]=value
        else: super().__setitem__(key,value)


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
        self.cpu = None

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
            masked = bool(self.cpu.p & self.cpu.INTERRUPT) if self.cpu else None
            self.writes.append((key, value, masked))
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

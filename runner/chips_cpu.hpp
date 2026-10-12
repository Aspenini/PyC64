#pragma once
#include "machine.hpp"
#include <ostream>

// Machine-code execution on chips' cycle-stepped NMOS 6502 with flat RAM.
// CHROUT ($FFD2) and GETIN ($FFE4) are serviced by the host; the program's
// final RTS lands on $0200, which ends the run as "returned".
struct CpuResult {
  std::vector<uint8_t> ram;
  std::string output, reason;
  uint64_t cycles = 0;
  uint16_t pc = 0;
  uint8_t a = 0, x = 0, y = 0, sp = 0, p = 0;
};
CpuResult execute_cpu(const std::vector<uint8_t> &program, uint16_t entry,
                      uint64_t limit, const std::string &input,
                      const Roms &roms, std::ostream *trace = nullptr);

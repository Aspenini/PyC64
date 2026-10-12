#include "chips_cpu.hpp"
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "chips/m6502.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <algorithm>
#include <cstdio>

CpuResult execute_cpu(const std::vector<uint8_t> &program, uint16_t entry,
                      uint64_t limit, const std::string &input,
                      const Roms &roms, std::ostream *trace) {
  CpuResult result;
  result.ram.resize(65536);
  uint16_t load = static_cast<uint16_t>(program[0] | (program[1] << 8));
  std::copy(program.begin() + 2, program.end(), result.ram.begin() + load);
  if (!roms.basic.empty()) {
    std::copy(roms.basic.begin(), roms.basic.end(),
              result.ram.begin() + 0xA000);
    // BASIC's cold start points the error vector (IERROR) at its handler.
    result.ram[0x300] = 0x3A;
    result.ram[0x301] = 0xA4;
  }
  if (!roms.kernal.empty())
    std::copy(roms.kernal.begin(), roms.kernal.end(),
              result.ram.begin() + 0xE000);
  result.ram[0xDC00] = result.ram[0xDC01] = 255;
  m6502_t cpu{};
  m6502_desc_t desc{};
  m6502_init(&cpu, &desc);
  cpu.A = cpu.X = cpu.Y = 0;
  cpu.P = 0x30;
  cpu.S = 0xEE;
  result.ram[0x1F0] = 0x01;
  result.ram[0x1EF] = 0xFF; // RTS to $0200
  uint64_t pins = 0;
  auto jump = [&](uint16_t address) {
    m6502_set_pc(&cpu, address);
    pins = M6502_MAKE_PINS(M6502_SYNC | M6502_RW, address, result.ram[address]);
  };
  auto return_from_hook = [&]() {
    uint16_t low = result.ram[0x100 + ++cpu.S];
    uint16_t high = result.ram[0x100 + ++cpu.S];
    jump(static_cast<uint16_t>((low | (high << 8)) + 1));
  };
  jump(entry);
  size_t input_offset = 0;
  result.reason = "cycle limit";
  while (result.cycles < limit) {
    if (pins & M6502_SYNC) {
      uint16_t pc = M6502_GET_ADDR(pins);
      if (trace) {
        char line[96];
        std::snprintf(line, sizeof line,
                      "%10llu %04X %02X  A=%02X X=%02X Y=%02X SP=%02X P=%02X\n",
                      static_cast<unsigned long long>(result.cycles), pc,
                      result.ram[pc], cpu.A, cpu.X, cpu.Y, cpu.S, cpu.P);
        *trace << line;
      }
      if (pc == 0x0200) {
        result.reason = "returned";
        break;
      }
      if (pc == 0xFFD2) {
        result.output += static_cast<char>(cpu.A);
        return_from_hook();
        continue;
      }
      if (pc == 0xFFE4) {
        uint8_t queued = result.ram[0xC6];
        if (queued && queued <= 10) {
          // Like the KERNAL, GETIN first drains the keyboard buffer.
          cpu.A = result.ram[0x277];
          std::copy(result.ram.begin() + 0x278,
                    result.ram.begin() + 0x277 + queued,
                    result.ram.begin() + 0x277);
          result.ram[0xC6] = static_cast<uint8_t>(queued - 1);
        } else if (input_offset < input.size()) {
          unsigned char character =
              static_cast<unsigned char>(input[input_offset++]);
          if (character >= 'a' && character <= 'z')
            character -= 'a' - 'A';
          cpu.A = character;
        } else if (!input.empty()) {
          // All scripted keys were used; waiting forever would hide that.
          result.reason = "input exhausted";
          break;
        } else
          cpu.A = 0;
        return_from_hook();
        continue;
      }
      if (result.ram[pc] == 0) {
        result.reason = "BRK guard";
        break;
      }
    }
    pins = m6502_tick(&cpu, pins);
    uint16_t address = M6502_GET_ADDR(pins);
    if (pins & M6502_RW) {
      M6502_SET_DATA(pins, result.ram[address]);
    } else
      result.ram[address] = M6502_GET_DATA(pins);
    ++result.cycles;
  }
  result.pc = M6502_GET_ADDR(pins);
  result.a = cpu.A;
  result.x = cpu.X;
  result.y = cpu.Y;
  result.sp = cpu.S;
  result.p = cpu.P;
  return result;
}

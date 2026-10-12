#include "machine.hpp"
#define mem_init chips_mem_init
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
// chips headers must follow their dependency order.
// clang-format off
#include "chips/chips_common.h"
#include "chips/m6502.h"
#include "chips/m6526.h"
#include "chips/m6569.h"
#include "chips/m6581.h"
#include "chips/kbd.h"
#include "chips/mem.h"
#include "chips/clk.h"
#include "systems/c1530.h"
#include "chips/m6522.h"
#include "systems/c1541.h"
#include "systems/c64.h"
// clang-format on
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#undef mem_init
#include <algorithm>
#include <array>
#include <stdexcept>

namespace {
class ChipsMachine final : public Machine {
  c64_t sys{};
  std::vector<uint32_t> image;
  std::vector<int16_t> sound;
  static void samples(const float *data, int count, void *context) {
    auto &output = static_cast<ChipsMachine *>(context)->sound;
    for (int i = 0; i < count; ++i)
      output.push_back(
          static_cast<int16_t>(std::clamp(data[i], -1.0f, 1.0f) * 32767));
  }

public:
  explicit ChipsMachine(const Roms &roms) {
    c64_desc_t desc{};
    desc.roms.basic = {const_cast<uint8_t *>(roms.basic.data()),
                       roms.basic.size()};
    desc.roms.kernal = {const_cast<uint8_t *>(roms.kernal.data()),
                        roms.kernal.size()};
    desc.roms.chars = {const_cast<uint8_t *>(roms.chars.data()),
                       roms.chars.size()};
    desc.audio.callback = {samples, this};
    desc.audio.sample_rate = 44100;
    desc.audio.num_samples = 512;
    c64_init(&sys, &desc);
    kbd_register_key(&sys.kbd, 0x80, 7, 1, 0); // explicit physical left Shift
    image.resize(static_cast<size_t>(width() * height()));
  }
  ~ChipsMachine() override { c64_discard(&sys); }
  void frame() override {
    c64_exec(&sys, 19951);
  } // exactly 19656 PAL phi2 ticks
  uint8_t read(uint16_t address) const override { return sys.ram[address]; }
  void write(uint16_t address, uint8_t value) override {
    sys.ram[address] = value;
  }
  uint16_t pc() const override { return sys.cpu.PC; }
  void key(int code, bool down) override {
    if (code == 0)
      code = 0x80;
    if (down)
      c64_key_down(&sys, code);
    else
      c64_key_up(&sys, code);
  }
  void joystick(uint8_t mask) override { c64_joystick(&sys, 0, mask); }
  const std::vector<uint32_t> &pixels() override {
    auto info = c64_display_info(&sys);
    const auto *indices = static_cast<const uint8_t *>(info.frame.buffer.ptr);
    const auto *colors = static_cast<const uint32_t *>(info.palette.ptr);
    for (int y = 0; y < height(); ++y)
      for (int x = 0; x < width(); ++x) {
        uint32_t abgr =
            colors[indices[(y + info.screen.y) * info.frame.dim.width + x +
                           info.screen.x]];
        image[static_cast<size_t>(y * width() + x)] =
            0xFF000000u | ((abgr & 0xFF) << 16) | (abgr & 0xFF00) |
            ((abgr >> 16) & 0xFF);
      }
    return image;
  }
  std::vector<int16_t> audio() override {
    std::vector<int16_t> result;
    result.swap(sound);
    return result;
  }
  int width() const override { return 392; }
  int height() const override { return 272; }
  bool halted() const override {
    // NMOS KIL opcodes retain the same decoder microcycle indefinitely.
    uint8_t op = static_cast<uint8_t>(sys.cpu.IR >> 3);
    return (op & 0x0F) == 2 && op != 0xA2 && op != 0xC2 && op != 0xE2 &&
           op != 0x82;
  }
  void start(uint16_t entry) override {
    if (entry)
      c64_basic_syscall(&sys, entry);
    else
      c64_basic_run(&sys);
  }
};
} // namespace

std::unique_ptr<Machine> make_chips(const Roms &roms) {
  return std::make_unique<ChipsMachine>(roms);
}

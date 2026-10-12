#include "machine.hpp"
extern "C" {
#include "cia.h"
#include "cpu.h"
#include "drive.h"
#include "iec.h"
#include "mem.h"
#include "sid.h"
#include "vic.h"
}
#include <stdexcept>

namespace {
class ChronoMachine final : public Machine {
  std::vector<uint32_t> image;

public:
  explicit ChronoMachine(const Roms &roms) {
    mem_init();
    if (!mem_load_rom(ROM_BASIC, roms.basic_path.c_str()) ||
        !mem_load_rom(ROM_KERNAL, roms.kernal_path.c_str()) ||
        !mem_load_rom(ROM_CHAR, roms.chars_path.c_str()))
      throw std::runtime_error("cannot load Chrono C64 ROMs");
    vic_init();
    cia_init();
    sid_init();
    cpu_init();
    cpu_reset();
    drive_init();
    iec_reset();
    if (!roms.drive_path.empty()) {
      if (!drive_load_rom(roms.drive_path.c_str()))
        throw std::runtime_error("invalid 1541 ROM");
      drive_reset();
    }
    sid_set_audio(true);
    image.resize(static_cast<size_t>(width() * height()));
  }
  void frame() override { iec_step_frame(); }
  uint8_t read(uint16_t address) const override { return mem_read(address); }
  void write(uint16_t address, uint8_t value) override {
    mem_write(address, value);
  }
  uint16_t pc() const override { return cpu.pc; }
  void key(int code, bool down) override;
  void joystick(uint8_t mask) override { cia_joy_set(1, mask); }
  const std::vector<uint32_t> &pixels() override {
    image.assign(vic_framebuffer(), vic_framebuffer() + image.size());
    return image;
  }
  std::vector<int16_t> audio() override {
    std::vector<int16_t> result(sid_audio_available());
    result.resize(
        sid_audio_read(result.data(), static_cast<unsigned>(result.size())));
    return result;
  }
  int width() const override { return vic_fb_width(); }
  int height() const override { return vic_fb_height(); }
  bool halted() const override { return cpu_halted() || cpu_jammed(); }
  void start(uint16_t entry) override {
    std::string command =
        entry ? "SYS" + std::to_string(entry) + "\r" : "RUN\r";
    for (size_t i = 0; i < command.size(); ++i)
      mem_write(static_cast<uint16_t>(0x277 + i),
                static_cast<uint8_t>(command[i]));
    mem_write(0xC6, static_cast<uint8_t>(command.size()));
  }
};

// C64 keyboard matrix (row:col); special codes match chips' public keyboard
// API.
void ChronoMachine::key(int code, bool down) {
  if (code == 0xFF) {
    cia_restore_set(down);
    return;
  }
  if (code == 0) {
    cia_key_set(1, 7, down);
    return;
  }
  struct Special {
    int code;
    unsigned row, col;
    bool shift;
  };
  static const Special special[] = {
      {32, 7, 4, false},   {8, 0, 2, true},     {9, 0, 2, false},
      {10, 0, 7, false},   {11, 0, 7, true},    {1, 0, 0, false},
      {16, 0, 0, true},    {12, 6, 3, false},   {2, 6, 3, true},
      {13, 0, 1, false},   {14, 7, 2, false},   {15, 7, 5, false},
      {3, 7, 7, false},    {7, 7, 7, true},     {4, 7, 1, false},
      {0xF1, 0, 4, false}, {0xF2, 0, 4, true},  {0xF3, 0, 5, false},
      {0xF4, 0, 5, true},  {0xF5, 0, 6, false}, {0xF6, 0, 6, true},
      {0xF7, 0, 3, false}, {0xF8, 0, 3, true}};
  for (const auto &key : special)
    if (key.code == code) {
      cia_key_set(key.row, key.col, down);
      if (key.shift)
        cia_key_set(1, 7, down);
      return;
    }
  static const char keys[] = "        "
                             "3WA4ZSE "
                             "5RD6CFTX"
                             "7YG8BHUV"
                             "9IJ0MKON"
                             "+PL-.:@,"
                             "~*;  = /"
                             "1  2  Q "
                             "        "
                             "#wa$zse "
                             "%rd&cftx"
                             "'yg(bhuv"
                             ")ij0mkon"
                             " pl >[ <"
                             "$ ]    ?"
                             "!  \"  q ";
  for (unsigned layer = 0; layer < 2; ++layer)
    for (unsigned index = 0; index < 64; ++index)
      if (keys[layer * 64 + index] != ' ' && keys[layer * 64 + index] == code) {
        cia_key_set(index / 8, index % 8, down);
        if (layer)
          cia_key_set(1, 7, down);
        return;
      }
}
} // namespace
std::unique_ptr<Machine> make_chrono(const Roms &roms) {
  return std::make_unique<ChronoMachine>(roms);
}

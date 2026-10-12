#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A complete PAL C64. pyc64run hosts Chrono's machine; pyc64test hosts chips'.
struct Machine {
  virtual ~Machine() = default;
  virtual void frame() = 0;
  virtual uint8_t
  read(uint16_t address) const = 0; // raw RAM, without device side effects
  virtual void write(uint16_t address, uint8_t value) = 0;
  virtual uint16_t pc() const = 0;
  virtual void key(int code, bool down) = 0;
  virtual void joystick(uint8_t mask) = 0;
  virtual const std::vector<uint32_t> &pixels() = 0; // ARGB8888
  virtual std::vector<int16_t> audio() = 0;          // mono 44100 Hz
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual bool halted() const = 0;
  virtual void start(uint16_t entry) = 0;
};

struct Roms {
  std::string basic_path, kernal_path, chars_path, drive_path;
  std::vector<uint8_t> basic, kernal, chars;
};
std::unique_ptr<Machine> make_chips(const Roms &roms);
std::unique_ptr<Machine> make_chrono(const Roms &roms);

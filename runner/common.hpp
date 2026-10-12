#pragma once
// Shared host code for pyc64run (Chrono) and pyc64test (chips): PRG loading,
// ROM discovery, option parsing, headless machine sessions and captures.
#include "machine.hpp"
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

std::vector<uint8_t> read_file(const fs::path &path);
void write_file(const fs::path &path, const void *data, size_t size);
uint64_t number(const std::string &text, uint64_t maximum);

struct Program {
  std::vector<uint8_t> bytes; // including the two-byte load address
  uint16_t load = 0, entry = 0;
};
// Validates the PRG and resolves its entry from --entry or a BASIC SYS stub.
Program load_program(const fs::path &path, uint16_t entry, bool entry_set);

struct Options {
  fs::path program, rom_dir;
  std::string basic, kernal, chars, input;
  std::vector<std::string> expected_text, expected_screen; // repeatable
  std::string ram_path, screenshot, audio_path, trace_path;
  std::vector<std::pair<uint16_t, uint8_t>> expectations;
  std::vector<std::pair<std::string, uint8_t>> registers;
  std::vector<int> held_keys;
  uint8_t joystick = 0;
  uint64_t frames = 0, cycles = 20000000;
  uint16_t entry = 0;
  bool entry_set = false, cycles_set = false;
  std::set<std::string> flags; // value-less switches such as --headless
};
// Parses PROGRAM.PRG followed by options; any option outside `allowed` is an
// error so each tool rejects settings its emulator cannot honour.
Options parse_options(int argc, char **argv,
                      const std::set<std::string> &allowed,
                      const std::set<std::string> &switches);

// ROMs come from explicit options, PYC64_*_ROM, then --rom-dir/PYC64_ROM_DIR
// using PyC64 or VICE file names.
Roms resolve_roms(const Options &options, bool required);

struct MachineRun {
  std::vector<uint8_t> ram;
  std::vector<int16_t> audio;
  std::string screen;
  uint64_t frames = 0;
};
// Boots the machine, injects the PRG, starts it and runs PAL frames. `present`
// is called after every frame; returning false ends the run (window closed).
using Presenter =
    std::function<bool(Machine &, const std::vector<int16_t> &)>;
MachineRun run_machine(Machine &machine, const Program &program,
                       const Options &options, const std::string &name,
                       const Presenter &present = {});

// Writes --ram/--screenshot/--audio captures and evaluates RAM and screen
// expectations, throwing on the first mismatch.
void finish_machine(Machine &machine, const MachineRun &run,
                    const Options &options);
void check_ram(const std::vector<uint8_t> &ram, const Options &options);
std::string petscii_text(const std::string &bytes);
std::string hex16(uint16_t value);

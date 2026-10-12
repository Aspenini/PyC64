#include "common.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <stdexcept>

std::vector<uint8_t> read_file(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("cannot read " + path.string());
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

void write_file(const fs::path &path, const void *data, size_t size) {
  std::ofstream file(path, std::ios::binary);
  if (!file || !file.write(static_cast<const char *>(data),
                           static_cast<std::streamsize>(size)))
    throw std::runtime_error("cannot write " + path.string());
}

uint64_t number(const std::string &text, uint64_t maximum) {
  std::string value = text;
  int base = 0;
  if (!value.empty() && value[0] == '$') {
    value.erase(0, 1);
    base = 16;
  }
  if (value.empty() || value[0] == '-' || value[0] == '+' ||
      value[0] == ' ')
    throw std::runtime_error("invalid number: " + text);
  size_t end = 0;
  uint64_t result = 0;
  try {
    result = std::stoull(value, &end, base);
  } catch (const std::exception &) {
    throw std::runtime_error("invalid number: " + text);
  }
  if (end != value.size() || result > maximum)
    throw std::runtime_error("number out of range: " + text);
  return result;
}

namespace {
std::string escaped_input(const std::string &value) {
  std::string result;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      char c = value[++i];
      if (c == 'n' || c == 'r')
        result += '\r';
      else if (c == '\\')
        result += '\\';
      else if (c == 'x' && i + 2 < value.size() &&
               std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
               std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
        // \xHH supplies a raw PETSCII key code, such as \x14 for DEL.
        result += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
        i += 2;
      } else
        throw std::runtime_error(
            "input supports \\n, \\r, \\\\ and \\xHH escapes");
    } else
      result += value[i] == '\n' ? '\r' : value[i];
  }
  return result;
}

std::string screen_text(const Machine &machine) {
  std::string result;
  // KERNAL text screen: $0288 is the high byte of its current screen address.
  uint16_t base = static_cast<uint16_t>(machine.read(0x288) << 8);
  for (unsigned row = 0; row < 25; ++row) {
    std::string line;
    for (unsigned col = 0; col < 40; ++col) {
      uint8_t code =
          machine.read(static_cast<uint16_t>(base + row * 40 + col)) & 127;
      line +=
          static_cast<char>(code < 32 ? code + 64 : (code < 64 ? code : ' '));
    }
    while (!line.empty() && line.back() == ' ')
      line.pop_back();
    result += line + '\n';
  }
  return result;
}

fs::path rom_path(const fs::path &directory, const std::string &explicit_path,
                  const char *environment,
                  std::initializer_list<const char *> names) {
  if (!explicit_path.empty())
    return explicit_path;
  if (const char *path = std::getenv(environment))
    if (*path)
      return path;
  if (!directory.empty())
    for (const char *name : names)
      if (fs::is_regular_file(directory / name))
        return directory / name;
  return {};
}

std::vector<uint8_t> rom_bytes(const fs::path &path, size_t length,
                               bool required) {
  if (path.empty()) {
    if (required)
      throw std::runtime_error(
          "missing C64 ROMs; use --rom-dir or explicit ROM options");
    return {};
  }
  auto data = read_file(path);
  if (data.size() != length)
    throw std::runtime_error(path.string() + ": incorrect ROM size (expected " +
                             std::to_string(length) + ")");
  return data;
}

void put16(std::vector<uint8_t> &data, uint16_t n) {
  data.push_back(static_cast<uint8_t>(n));
  data.push_back(static_cast<uint8_t>(n >> 8));
}
void put32(std::vector<uint8_t> &data, uint32_t n) {
  put16(data, static_cast<uint16_t>(n));
  put16(data, static_cast<uint16_t>(n >> 16));
}
void put_tag(std::vector<uint8_t> &data, const char *tag) {
  data.insert(data.end(), tag, tag + 4);
}

void write_wav(const fs::path &path, const std::vector<int16_t> &samples) {
  if (samples.size() > (UINT32_MAX - 36) / 2)
    throw std::runtime_error("audio capture exceeds WAV size limit");
  std::vector<uint8_t> data;
  uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
  put_tag(data, "RIFF");
  put32(data, 36 + bytes);
  put_tag(data, "WAVE");
  put_tag(data, "fmt ");
  put32(data, 16);
  put16(data, 1);
  put16(data, 1);
  put32(data, 44100);
  put32(data, 88200);
  put16(data, 2);
  put16(data, 16);
  put_tag(data, "data");
  put32(data, bytes);
  for (int16_t sample : samples)
    put16(data, static_cast<uint16_t>(sample));
  write_file(path, data.data(), data.size());
}

// 32-bit BI_RGB bitmap; ARGB8888 words are already BMP's little-endian BGRA.
void write_bmp(const fs::path &path, const std::vector<uint32_t> &pixels,
               int width, int height) {
  uint32_t image = static_cast<uint32_t>(width * height * 4);
  std::vector<uint8_t> data;
  data.reserve(54 + image);
  data.push_back('B');
  data.push_back('M');
  put32(data, 54 + image);
  put32(data, 0);
  put32(data, 54);
  put32(data, 40);
  put32(data, static_cast<uint32_t>(width));
  put32(data, static_cast<uint32_t>(height));
  put16(data, 1);
  put16(data, 32);
  put32(data, 0);
  put32(data, image);
  put32(data, 2835); // 72 DPI
  put32(data, 2835);
  put32(data, 0);
  put32(data, 0);
  for (int y = height - 1; y >= 0; --y)
    for (int x = 0; x < width; ++x)
      put32(data, pixels[static_cast<size_t>(y * width + x)]);
  write_file(path, data.data(), data.size());
}
} // namespace

std::string hex16(uint16_t value) {
  char text[6];
  std::snprintf(text, sizeof text, "$%04X", value);
  return text;
}

std::string petscii_text(const std::string &bytes) {
  std::string result;
  for (unsigned char c : bytes) {
    if (c == 13)
      result += '\n';
    else if (c >= 32 && c < 127)
      result += static_cast<char>(c);
    else if (c >= 193 && c <= 218)
      result += static_cast<char>(c - 128);
  }
  return result;
}

Program load_program(const fs::path &path, uint16_t entry, bool entry_set) {
  Program program;
  program.bytes = read_file(path);
  const auto &bytes = program.bytes;
  if (bytes.size() < 3)
    throw std::runtime_error("PRG needs a two-byte load address and payload");
  program.load = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
  if (bytes.size() - 2 > 65536u - program.load)
    throw std::runtime_error("PRG payload exceeds C64 memory");
  program.entry = entry;
  if (entry_set || program.load != 0x801)
    return program;
  // A BASIC SYS stub has a line link, line number, SYS token and decimal entry.
  if (bytes.size() > 7 && bytes[6] == 0x9E) {
    size_t i = 7;
    while (i < bytes.size() && bytes[i] == ' ')
      ++i;
    std::string digits;
    while (i < bytes.size() && bytes[i] >= '0' && bytes[i] <= '9')
      digits += static_cast<char>(bytes[i++]);
    if (!digits.empty())
      program.entry = static_cast<uint16_t>(number(digits, 65535));
  }
  return program;
}

Options parse_options(int argc, char **argv,
                      const std::set<std::string> &allowed,
                      const std::set<std::string> &switches) {
  if (argc < 2)
    throw std::runtime_error("missing PRG file; see --help");
  Options options;
  options.program = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string option = argv[i];
    if (switches.count(option)) {
      options.flags.insert(option);
      continue;
    }
    if (!allowed.count(option))
      throw std::runtime_error("unknown option " + option + "; see --help");
    if (i + 1 == argc)
      throw std::runtime_error("missing value for " + option);
    std::string value = argv[++i];
    if (option == "--rom-dir")
      options.rom_dir = value;
    else if (option == "--basic-rom")
      options.basic = value;
    else if (option == "--kernal-rom")
      options.kernal = value;
    else if (option == "--chargen-rom")
      options.chars = value;
    else if (option == "--frames") {
      options.frames = number(value, 1000000);
      if (!options.frames)
        throw std::runtime_error("frames must be positive");
    } else if (option == "--cycles") {
      options.cycles = number(value, 1000000000);
      options.cycles_set = true;
      if (!options.cycles)
        throw std::runtime_error("cycles must be positive");
    } else if (option == "--entry") {
      options.entry = static_cast<uint16_t>(number(value, 65535));
      options.entry_set = true;
    } else if (option == "--input")
      options.input = escaped_input(value);
    else if (option == "--key")
      options.held_keys.push_back(static_cast<int>(number(value, 255)));
    else if (option == "--joystick")
      options.joystick = static_cast<uint8_t>(number(value, 31));
    else if (option == "--expect-text")
      options.expected_text.push_back(value);
    else if (option == "--expect-screen")
      options.expected_screen.push_back(value);
    else if (option == "--ram")
      options.ram_path = value;
    else if (option == "--screenshot")
      options.screenshot = value;
    else if (option == "--audio")
      options.audio_path = value;
    else if (option == "--trace")
      options.trace_path = value;
    else if (option == "--expect" || option == "--expect-reg") {
      size_t split = value.find('=');
      if (split == std::string::npos)
        throw std::runtime_error(option + " needs " +
                                 (option == "--expect" ? "ADDRESS=BYTE"
                                                       : "REGISTER=BYTE"));
      std::string target = value.substr(0, split);
      uint8_t byte = static_cast<uint8_t>(number(value.substr(split + 1), 255));
      if (option == "--expect")
        options.expectations.emplace_back(
            static_cast<uint16_t>(number(target, 65535)), byte);
      else {
        for (char &c : target)
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (target != "A" && target != "X" && target != "Y" &&
            target != "SP" && target != "P")
          throw std::runtime_error("registers are A, X, Y, SP and P");
        options.registers.emplace_back(target, byte);
      }
    } else
      throw std::runtime_error("unhandled option " + option);
  }
  return options;
}

Roms resolve_roms(const Options &options, bool required) {
  fs::path directory = options.rom_dir;
  if (directory.empty())
    if (const char *dir = std::getenv("PYC64_ROM_DIR"))
      directory = dir;
  Roms roms;
  roms.basic_path = rom_path(directory, options.basic, "PYC64_BASIC_ROM",
                             {"basic.rom", "basic-901226-01.bin"})
                        .string();
  roms.kernal_path = rom_path(directory, options.kernal, "PYC64_KERNAL_ROM",
                              {"kernal.rom", "kernal-901227-03.bin"})
                         .string();
  roms.chars_path = rom_path(directory, options.chars, "PYC64_CHARGEN_ROM",
                             {"chargen.rom", "chargen-901225-01.bin"})
                        .string();
  roms.basic = rom_bytes(roms.basic_path, 8192, required);
  roms.kernal = rom_bytes(roms.kernal_path, 8192, required);
  roms.chars = rom_bytes(roms.chars_path, 4096, required);
  return roms;
}

MachineRun run_machine(Machine &machine, const Program &program,
                       const Options &options, const std::string &name,
                       const Presenter &present) {
  if (!program.entry && program.load != 0x801)
    throw std::runtime_error("non-BASIC PRG needs --entry ADDRESS");
  // Let the KERNAL reach READY before the PRG replaces BASIC's program area.
  for (int i = 0; i < 150; ++i) {
    machine.frame();
    machine.audio();
  }
  const auto &bytes = program.bytes;
  for (size_t i = 2; i < bytes.size(); ++i)
    machine.write(static_cast<uint16_t>(program.load + i - 2), bytes[i]);
  uint16_t end = static_cast<uint16_t>(program.load + bytes.size() - 2);
  // BASIC's start-of-variables, arrays, strings and LOAD end pointers.
  for (uint16_t pointer :
       std::initializer_list<uint16_t>{0x2D, 0x2F, 0x31, 0x33, 0xAE}) {
    machine.write(static_cast<uint16_t>(pointer), static_cast<uint8_t>(end));
    machine.write(static_cast<uint16_t>(pointer + 1),
                  static_cast<uint8_t>(end >> 8));
  }
  machine.start(program.entry);
  for (int code : options.held_keys)
    machine.key(code, true);
  machine.joystick(options.joystick);
  MachineRun run;
  size_t input_offset = 0;
  while (!options.frames || run.frames < options.frames) {
    // Wait for SYS/RUN to drain before supplying input, one key at a time.
    if (run.frames >= 5 && input_offset < options.input.size() &&
        machine.read(0xC6) == 0) {
      unsigned char c =
          static_cast<unsigned char>(options.input[input_offset++]);
      if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
      machine.write(0x277, c);
      machine.write(0xC6, 1);
    }
    machine.frame();
    ++run.frames;
    auto samples = machine.audio();
    if (!options.audio_path.empty())
      run.audio.insert(run.audio.end(), samples.begin(), samples.end());
    if (machine.halted())
      throw std::runtime_error(name + " CPU halted at " + hex16(machine.pc()));
    if (present && !present(machine, samples))
      break;
  }
  run.screen = screen_text(machine);
  run.ram.resize(65536);
  for (unsigned i = 0; i < 65536; ++i)
    run.ram[i] = machine.read(static_cast<uint16_t>(i));
  return run;
}

void check_ram(const std::vector<uint8_t> &ram, const Options &options) {
  for (const auto &check : options.expectations)
    if (ram[check.first] != check.second)
      throw std::runtime_error("RAM[" + std::to_string(check.first) +
                               "] = " + std::to_string(ram[check.first]) +
                               ", expected " + std::to_string(check.second));
}

void finish_machine(Machine &machine, const MachineRun &run,
                    const Options &options) {
  if (!options.ram_path.empty())
    write_file(options.ram_path, run.ram.data(), run.ram.size());
  if (!options.audio_path.empty())
    write_wav(options.audio_path, run.audio);
  if (!options.screenshot.empty())
    write_bmp(options.screenshot, machine.pixels(), machine.width(),
              machine.height());
  check_ram(run.ram, options);
  for (const auto &text : options.expected_screen)
    if (run.screen.find(text) == std::string::npos)
      throw std::runtime_error("expected screen text not found: " + text);
}

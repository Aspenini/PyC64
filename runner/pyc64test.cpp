// pyc64test: headless machine-code testing on floooh/chips.
#include "chips_cpu.hpp"
#include "common.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void help() {
  std::cout
      << "PyC64 tester 0.1.0 (floooh/chips)\n"
         "Usage: pyc64test PROGRAM.PRG [options]\n\n"
         "CPU mode (default) runs the PRG on chips' cycle-stepped 6502 with\n"
         "flat RAM. CHROUT and GETIN are serviced by the tester; the test\n"
         "passes when the program returns and every check holds.\n"
         "  --entry ADDRESS             Override the PRG's BASIC SYS entry\n"
         "  --cycles N                  Execution limit (default 20000000)\n"
         "  --input TEXT                GETIN text; \\r presses Return and\n"
         "                              \\xHH sends a PETSCII code\n"
         "  --expect ADDRESS=BYTE       Check a RAM byte (repeatable)\n"
         "  --expect-reg REG=BYTE       Check A, X, Y, SP or P at return\n"
         "  --expect-text TEXT          Check CHROUT output contains text\n"
         "                              (--expect-* options are repeatable)\n"
         "  --ram FILE                  Write a 65536-byte RAM dump\n"
         "  --trace FILE                Log every instruction and register\n"
         "  --rom-dir DIR / --basic-rom FILE / --kernal-rom FILE\n"
         "                              Optional ROM images mapped into RAM\n\n"
         "Machine mode (--machine) runs chips' complete PAL C64 headless.\n"
         "It needs basic, kernal and chargen ROMs and at least one --expect\n"
         "or --expect-screen check.\n"
         "  --frames N                  PAL frames to run (default 300)\n"
         "  --input TEXT                Typed into the keyboard buffer\n"
         "  --key CODE --joystick MASK  Hold a C64 key / joystick-2 bits\n"
         "  --expect ADDRESS=BYTE       Check a RAM byte (repeatable)\n"
         "  --expect-screen TEXT        Check the text screen contains text\n"
         "  --ram FILE --screenshot FILE.bmp --audio FILE.wav  Captures\n"
         "  --rom-dir DIR               basic.rom, kernal.rom, chargen.rom\n"
         "  --basic-rom FILE --kernal-rom FILE --chargen-rom FILE\n\n"
         "Numbers accept decimal, 0x or $ hexadecimal. Exit status is 0 when\n"
         "all checks pass and 1 otherwise. Use pyc64run for interactive play.\n";
}

void reject(bool present, const char *option, const char *mode) {
  if (present)
    throw std::runtime_error(std::string(option) + " is not available in " +
                             mode + " mode");
}

void cpu_mode(const Options &options) {
  reject(!options.held_keys.empty(), "--key", "CPU");
  reject(options.joystick != 0, "--joystick", "CPU");
  reject(options.frames != 0, "--frames", "CPU");
  reject(!options.expected_screen.empty(), "--expect-screen", "CPU");
  reject(!options.screenshot.empty(), "--screenshot", "CPU");
  reject(!options.audio_path.empty(), "--audio", "CPU");
  reject(!options.chars.empty(), "--chargen-rom", "CPU");
  Program program =
      load_program(options.program, options.entry, options.entry_set);
  if (!program.entry)
    throw std::runtime_error(
        "CPU mode needs a BASIC SYS stub or --entry ADDRESS");
  Roms roms = resolve_roms(options, false);
  std::ofstream trace_file;
  if (!options.trace_path.empty()) {
    trace_file.open(options.trace_path, std::ios::binary);
    if (!trace_file)
      throw std::runtime_error("cannot write " + options.trace_path);
  }
  auto result =
      execute_cpu(program.bytes, program.entry, options.cycles, options.input,
                  roms, trace_file.is_open() ? &trace_file : nullptr);
  std::string output = petscii_text(result.output);
  std::cout << output << std::flush;
  char registers[64];
  std::snprintf(registers, sizeof registers,
                "A=$%02X X=$%02X Y=$%02X SP=$%02X P=$%02X", result.a,
                result.x, result.y, result.sp, result.p);
  std::cerr << "cpu: " << result.reason << ", " << result.cycles
            << " cycles, PC=" << hex16(result.pc) << ' ' << registers << '\n';
  if (!options.ram_path.empty())
    write_file(options.ram_path, result.ram.data(), result.ram.size());
  // RAM checks also apply to a program stopped by a guard, which lets a test
  // prove that the trap happened before any hardware write.
  check_ram(result.ram, options);
  if (result.reason != "returned")
    throw std::runtime_error("program did not return: " + result.reason);
  for (const auto &check : options.registers) {
    uint8_t actual = check.first == "A"    ? result.a
                     : check.first == "X"  ? result.x
                     : check.first == "Y"  ? result.y
                     : check.first == "SP" ? result.sp
                                           : result.p;
    if (actual != check.second)
      throw std::runtime_error(check.first + " = " + std::to_string(actual) +
                               ", expected " + std::to_string(check.second));
  }
  for (const auto &text : options.expected_text)
    if (output.find(text) == std::string::npos)
      throw std::runtime_error("expected text not found: " + text);
}

void machine_mode(Options options) {
  reject(options.cycles_set, "--cycles", "machine");
  reject(!options.registers.empty(), "--expect-reg", "machine");
  reject(!options.expected_text.empty(), "--expect-text", "machine");
  reject(!options.trace_path.empty(), "--trace", "machine");
  if (options.expectations.empty() && options.expected_screen.empty())
    throw std::runtime_error("machine mode needs --expect or --expect-screen");
  Program program =
      load_program(options.program, options.entry, options.entry_set);
  Roms roms = resolve_roms(options, true);
  if (!options.frames)
    options.frames = 300;
  auto machine = make_chips(roms);
  auto run = run_machine(*machine, program, options, "chips");
  std::cout << run.screen << std::flush;
  std::cerr << "chips: " << run.frames << " PAL frames, PC="
            << hex16(machine->pc()) << '\n';
  finish_machine(*machine, run, options);
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::string first = argc > 1 ? argv[1] : "";
    if (argc == 1 || first == "--help" || first == "-h") {
      help();
      return 0;
    }
    if (first == "--version") {
      std::cout << "pyc64test 0.1.0\n";
      return 0;
    }
    Options options = parse_options(
        argc, argv,
        {"--rom-dir", "--basic-rom", "--kernal-rom", "--chargen-rom",
         "--entry", "--cycles", "--frames", "--input", "--key", "--joystick",
         "--expect", "--expect-reg", "--expect-text", "--expect-screen",
         "--ram", "--trace", "--screenshot", "--audio"},
        {"--machine"});
    if (options.flags.count("--machine"))
      machine_mode(options);
    else
      cpu_mode(options);
    std::cerr << "Checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "pyc64test: " << error.what() << '\n';
    return 1;
  }
}

// pyc64run: plays a PRG on ChronoCoders/C64 with SDL video, audio and input.
#include "common.hpp"
#include <SDL.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
int key_code(SDL_Keycode key) {
  if (key >= SDLK_a && key <= SDLK_z)
    return static_cast<int>(key - SDLK_a + 'A');
  if (key >= SDLK_0 && key <= SDLK_9)
    return static_cast<int>(key);
  if (key >= SDLK_F1 && key <= SDLK_F8)
    return 0xF1 + static_cast<int>(key - SDLK_F1);
  switch (key) {
  case SDLK_RETURN:
    return 13;
  case SDLK_BACKSPACE:
    return 1;
  case SDLK_LEFT:
    return 8;
  case SDLK_RIGHT:
    return 9;
  case SDLK_UP:
    return 11;
  case SDLK_DOWN:
    return 10;
  case SDLK_ESCAPE:
    return 3;
  case SDLK_HOME:
    return 12;
  case SDLK_PAGEUP:
    return 255;
  case SDLK_TAB:
    return 14;
  case SDLK_LALT:
    return 15;
  case SDLK_LSHIFT:
  case SDLK_RSHIFT:
    return 0;
  default:
    return key >= 32 && key <= 126 ? static_cast<int>(key) : -1;
  }
}

struct Display {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  SDL_AudioDeviceID sound = 0;
  std::chrono::steady_clock::time_point next =
      std::chrono::steady_clock::now();

  Display(Machine &machine, const std::string &title) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0)
      throw std::runtime_error(SDL_GetError());
    window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, machine.width() * 2,
                              machine.height() * 2, SDL_WINDOW_RESIZABLE);
    if (window)
      renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer && window)
      renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer)
      texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, machine.width(),
                                  machine.height());
    if (!texture) {
      std::string error = SDL_GetError();
      shutdown();
      throw std::runtime_error(error);
    }
    SDL_RenderSetLogicalSize(renderer, machine.width(), machine.height());
    SDL_AudioSpec spec{};
    spec.freq = 44100;
    spec.format = AUDIO_S16SYS;
    spec.channels = 1;
    spec.samples = 1024;
    sound = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (sound)
      SDL_PauseAudioDevice(sound, 0);
    else
      std::cerr << "Audio unavailable: " << SDL_GetError() << '\n';
  }
  void shutdown() {
    if (sound)
      SDL_CloseAudioDevice(sound);
    if (texture)
      SDL_DestroyTexture(texture);
    if (renderer)
      SDL_DestroyRenderer(renderer);
    if (window)
      SDL_DestroyWindow(window);
    SDL_Quit();
  }
  ~Display() { shutdown(); }
  Display(const Display &) = delete;
  Display &operator=(const Display &) = delete;

  bool update(Machine &machine, const std::vector<int16_t> &samples) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT)
        return false;
      if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        if (event.key.keysym.sym == SDLK_F12 && event.type == SDL_KEYDOWN)
          return false;
        int code = key_code(event.key.keysym.sym);
        if (code >= 0)
          machine.key(code, event.type == SDL_KEYDOWN);
      }
      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        for (int code = 0; code < 256; ++code)
          machine.key(code, false);
      }
    }
    const uint8_t *keys = SDL_GetKeyboardState(nullptr);
    machine.key(0, keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
    uint8_t mask = static_cast<uint8_t>(
        (keys[SDL_SCANCODE_KP_8] ? 1 : 0) | (keys[SDL_SCANCODE_KP_2] ? 2 : 0) |
        (keys[SDL_SCANCODE_KP_4] ? 4 : 0) | (keys[SDL_SCANCODE_KP_6] ? 8 : 0) |
        (keys[SDL_SCANCODE_RCTRL] ? 16 : 0));
    machine.joystick(mask);
    const auto &pixels = machine.pixels();
    SDL_UpdateTexture(texture, nullptr, pixels.data(),
                      machine.width() * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    if (sound && !samples.empty() &&
        SDL_QueueAudio(sound, samples.data(),
                       static_cast<Uint32>(samples.size() * sizeof(int16_t))) !=
            0)
      throw std::runtime_error(SDL_GetError());
    // Pace to the PAL frame rate: 19656 cycles at 985248 Hz.
    next += std::chrono::nanoseconds(19950598);
    auto now = std::chrono::steady_clock::now();
    if (next > now)
      SDL_Delay(static_cast<Uint32>(
          std::chrono::duration_cast<std::chrono::milliseconds>(next - now)
              .count()));
    else if (now - next > std::chrono::milliseconds(100))
      next = now;
    return true;
  }
};

void help() {
  std::cout
      << "PyC64 runner 0.1.0 (ChronoCoders/C64)\n"
         "Usage: pyc64run PROGRAM.PRG [options]\n\n"
         "Opens a PAL C64 window with video, keyboard, joystick and SID audio.\n"
         "  --rom-dir DIR               basic.rom, kernal.rom, chargen.rom "
         "(VICE names also work)\n"
         "  --basic-rom FILE --kernal-rom FILE --chargen-rom FILE\n"
         "  --entry ADDRESS             Override the PRG's BASIC SYS entry\n"
         "  --input TEXT                Type text; \\r presses Return, \\xHH\n"
         "                              sends a PETSCII code\n"
         "  --key CODE --joystick MASK  Hold a C64 key / joystick-2 bits\n"
         "  --frames N                  Stop after N PAL frames\n"
         "  --headless                  No window or audio device; runs "
         "--frames\n"
         "                              (default 300) and prints the screen\n"
         "  --expect ADDRESS=BYTE       Check a RAM byte on exit (repeatable)\n"
         "  --expect-screen TEXT        Check the text screen on exit (repeatable)\n"
         "  --ram FILE --screenshot FILE.bmp --audio FILE.wav  Captures on "
         "exit\n\n"
         "ROMs may also come from PYC64_ROM_DIR or PYC64_BASIC_ROM, "
         "PYC64_KERNAL_ROM\nand PYC64_CHARGEN_ROM. Exit status is 1 if a check "
         "fails.\n"
         "Window controls: F12 closes, Escape = RUN/STOP, Page Up = RESTORE,\n"
         "Tab = CTRL, Left Alt = Commodore; numpad 8/2/4/6 + Right Ctrl = "
         "joystick 2.\n"
         "Use pyc64test for machine-code level tests.\n";
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
      std::cout << "pyc64run 0.1.0\n";
      return 0;
    }
    Options options = parse_options(
        argc, argv,
        {"--rom-dir", "--basic-rom", "--kernal-rom", "--chargen-rom",
         "--entry", "--frames", "--input", "--key", "--joystick", "--expect",
         "--expect-screen", "--ram", "--screenshot", "--audio"},
        {"--headless"});
    bool headless = options.flags.count("--headless") != 0;
    Program program =
        load_program(options.program, options.entry, options.entry_set);
    Roms roms = resolve_roms(options, true);
    if (headless && !options.frames)
      options.frames = 300;
    auto machine = make_chrono(roms);
    std::unique_ptr<Display> display;
    Presenter present;
    if (!headless) {
      display = std::make_unique<Display>(
          *machine,
          "PyC64 - " + options.program.filename().string() + " (F12 closes)");
      present = [&](Machine &m, const std::vector<int16_t> &samples) {
        return display->update(m, samples);
      };
    }
    auto run = run_machine(*machine, program, options, "chrono", present);
    display.reset();
    if (headless)
      std::cout << run.screen << std::flush;
    std::cerr << "chrono: " << run.frames << " PAL frames, PC="
              << hex16(machine->pc()) << '\n';
    finish_machine(*machine, run, options);
    if (!options.expectations.empty() || !options.expected_screen.empty())
      std::cerr << "Checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "pyc64run: " << error.what() << '\n';
    return 1;
  }
}

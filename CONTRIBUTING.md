# Contributing to midi2cpp

Thank you for your interest in contributing to midi2cpp.

## Reporting Issues

Open an issue with:
- What you expected
- What happened
- Minimal code to reproduce
- Platform, board, and toolchain version (Pico SDK / ESP-IDF / TinyUSB CMake / PlatformIO)

## Code Contributions

1. Fork the repository
2. Create a branch (`git checkout -b fix/description`)
3. Make your changes
4. Run host tests:
   ```bash
   cmake -B build -DMIDI2CPP_BUILD_TESTS=ON
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ```
5. Run sanitizers locally:
   ```bash
   cmake -B build-asan -DMIDI2CPP_BUILD_TESTS=ON \
     -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g" \
     -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
   cmake --build build-asan --parallel
   ctest --test-dir build-asan --output-on-failure
   ```
6. Open a pull request with a clear description

## Code Style

- **C++17 strict** (`-Wall -Wextra -Wpedantic -Werror`, zero warnings on GCC and Clang)
- **Static-by-default** -- the hot path is allocation-free; init-time `new` is allowed only inside `m2bridge` for the per-slot tables. The C99 core (midi2) stays strictly zero-allocation
- **Caller-provided platform hooks** -- write fn, monotonic clock, RNG, USB mount/alt are caller-supplied; no `<Arduino.h>`, `pico/time.h`, `esp_timer.h` or USB stack header is included by the library
- **Naming**: `m2device` / `m2host` / `m2bridge` / `m2ci` aliases; class methods camelCase; preprocessor macros `MIDI2CPP_*`
- **Every new public method needs a test** -- host-side unit tests under `tests/`
- **C++17 standard library only** -- `<cstdint>`, `<cstring>`, `<functional>`, `<array>`. No exceptions, no RTTI assumptions
- **Comments**: explain _why_, not _what_. Reference the spec section when handling a UMP or MIDI-CI message

## Adding a Hardware Recipe

Recipes live under `examples/<board-role>-midi2/` and follow a consistent layout:

- `README.md` (banner, USB identity, build, validation, spec coverage table)
- `idf/`, `pio/`, or top-level `CMakeLists.txt` depending on the build system
- `src/` (board glue, USB descriptors, showcase main)
- `board/` (banner image, schematic, datasheet)
- `monitor/` (screenshots from MIDI tooling that prove the recipe runs)
- A fresh USB PID from the project pool

The simplest way to add a new recipe is to copy the closest existing one under `examples/` and adapt the board glue, USB descriptors, and showcase main to the new target.

## Testing

```bash
cmake -B build -DMIDI2CPP_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure                         # all tests
arduino-cli compile examples/hello-midi2-arduino --fqbn rp2040:rp2040:rpipico  # Arduino path
```

CI runs host tests on Linux + macOS, strict warnings (gcc + clang), arduino-cli compile of `hello-midi2-arduino.ino`, and the Pico SDK build of `examples/rp2040-midi2`.

## Spec References

midi2cpp wraps the [midi2](https://github.com/sauloverissimo/midi2) C99 core, which implements:

- **UMP messages**: M2-104-UM v1.1.2
- **MIDI-CI messages**: M2-101-UM v1.2
- **Value scaling**: M2-115-U v1.0.2

When adding or modifying message handling, reference the spec section in code comments.

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

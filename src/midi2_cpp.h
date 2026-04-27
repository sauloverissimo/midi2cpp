// midi2_cpp v0.1.0
// C++17 Arduino-style wrapper for MIDI 2.0 on embedded devices.
// Built over the portable midi2 C99 library (vendored at src/midi2.h).
//
// Usage in sketch:
//   #include <midi2_cpp.h>
//   using namespace midi2;
//   Device midi;
//   void setup() { midi.begin(); }
//   void loop()  { midi.task(); }
//
// For desktop C++ MIDI 2.0, consider ni-midi2 (Native Instruments).
// For a different embedded C++ design philosophy, see AM_MIDI2.0Lib (Andrew Mee).

#pragma once

// C++17 floor. Every supported toolchain in 2026 ships C++17 or newer:
// ESP-IDF v5+ (gcc 14), arduino-pico (gcc-arm-none-eabi 14), Pico SDK 2,
// Teensyduino 1.58+ (gcc-arm-none-eabi 11.3), libDaisy / STM32 (gcc 10+),
// Adafruit nRF52 / SAMD (gcc 9+). AVR Uno is out of scope by RAM, not by
// language. A clean diagnostic now beats a cryptic template error later.
#if __cplusplus < 201703L
#  error "midi2_cpp requires C++17 or newer. Set CMAKE_CXX_STANDARD=17 (or pass -std=c++17 to your build)."
#endif

// C99 core (vendored stb-style). User apps can also access midi2_* functions directly.
#include "midi2.h"

// C++ wrapper classes in namespace midi2::
#include "midi2_device.h"
#include "midi2_ci.h"
#include "midi2_host.h"

namespace midi2 {

// Ergonomic aliases for user code. The canonical class names are Device,
// CI, and Host; the aliases below let sketches use a shorter form
// without renaming anything internal. Usable in sketches via
// `using namespace midi2;` (idiomatic Arduino style) or fully qualified
// as `midi2::m2device`. v0.1 ships the device + host shapes; m2bridge
// (composition of host + device with an UMP router) lands in v0.2.
using m2device = Device;
using m2ci     = CI;
using m2host   = Host;

}  // namespace midi2

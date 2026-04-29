# midi2_cpp

## 🎹 MIDI 2.0 engine for embedded systems

*C++17, callback-first, zero-allocation, zero external dependencies, MIT.* From DIY to professional products.

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://en.cppreference.com/cpp/compiler_support)
[![MIDI 2.0](https://img.shields.io/badge/MIDI-2.0-blueviolet.svg)](https://midi.org/specifications/midi-2-0-specifications)
[![external deps](https://img.shields.io/badge/external%20deps-zero-success.svg)](#zero-external-dependencies)
[![Arduino](https://img.shields.io/badge/Arduino-IDE-00979D.svg)](https://www.arduino.cc/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Registry-FF7F00.svg)](https://registry.platformio.org/libraries/sauloverissimo/midi2_cpp)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-E7352C.svg)](https://docs.espressif.com/projects/esp-idf/en/stable/)
[![Pico SDK](https://img.shields.io/badge/Pico_SDK-2.0-C51A4A.svg)](https://github.com/raspberrypi/pico-sdk)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-064F8C.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

## The library

midi2_cpp is the layer where a sketch meets the protocol. Plug a board into the laptop, write five lines of C++, flash, and the device appears on the bus as a USB MIDI 2.0 endpoint with full Capability Inquiry, Property Exchange, and 32-bit resolution.

Underneath, midi2 (the portable C99 core) handles parsing, dispatch, and reassembly. midi2_cpp adds the C++ ergonomics: callbacks, board glue, ready-made USB descriptors. The board does the talking; the sketch tells it what to say.

## Contents

- [midi2\_cpp](#midi2_cpp)
  - [🎹 MIDI 2.0 engine for embedded systems](#-midi-20-engine-for-embedded-systems)
  - [The library](#the-library)
  - [Contents](#contents)
  - [Zero external dependencies](#zero-external-dependencies)
  - [Quickstart](#quickstart)
  - [What you get](#what-you-get)
  - [Three shapes](#three-shapes)
  - [Boards](#boards)
    - [Coming soon](#coming-soon)
  - [Install](#install)
    - [Arduino IDE](#arduino-ide)
    - [PlatformIO](#platformio)
    - [ESP-IDF component](#esp-idf-component)
    - [CMake FetchContent](#cmake-fetchcontent)
    - [Git submodule](#git-submodule)
    - [Manual vendor](#manual-vendor)
  - [API at a glance](#api-at-a-glance)
  - [Architecture](#architecture)
  - [What this library is not](#what-this-library-is-not)
  - [Sponsor](#sponsor)
  - [About](#about)
  - [Specifications and trademarks](#specifications-and-trademarks)
  - [License](#license)

## Zero external dependencies

midi2_cpp is a single self-contained tree:

- **No submodules.** `git clone` is the install. No `--recurse-submodules`, no `git submodule update`, no half-initialised state.
- **No fetch from another registry.** The midi2 C99 backing lives at [`src/midi2.{h,c}`](src/), vendored stb-style. One source of truth, audited together, versioned together.
- **No transport library pulled in.** TinyUSB, Teensy native USB, PIO-USB, libDaisy USBMidi: all caller-supplied. The library does not include `<Arduino.h>`, `pico/time.h`, `esp_timer.h`, or any USB header.
- **No clock or RNG dependency.** Caller injects `millis` / `time_us_64` / `esp_timer_get_time` and `random` / `get_rand_32` / `esp_random` through public hooks. Unset hooks degrade silently, no missing-symbol link errors.

```bash
git clone https://github.com/sauloverissimo/midi2_cpp.git
cmake -B build && cmake --build build && ctest --test-dir build
```

Three commands. Anywhere C++17 + CMake reach: Linux, macOS, Windows, embedded cross-toolchains for ARM/RISC-V/Xtensa. The library compiles, links, and self-tests with no network access after the initial clone.

## Quickstart

`midi2_cpp` is platform-agnostic: it parses, dispatches, and assembles UMP, and it leaves USB transport, clock, and entropy to the caller. The sketch wires four hooks to its platform's USB MIDI driver; the library does the rest.

```cpp
#include <midi2_cpp.h>
using namespace midi2;

m2device midi;
m2ci     ci(midi);

// 1. Outbound UMP. Forward to the platform's USB MIDI write API.
void plat_write(const uint32_t* words, size_t count) {
  // tud_midi_n_stream_write(0, (uint8_t*)words, count * 4);  // TinyUSB
  // usbMIDI.sendUMP(words, count);                           // Teensy native
  // ...
}

// 2. Monotonic millisecond clock. Used by the JR Heartbeat.
uint32_t plat_now() { return millis(); }

// 3. Entropy source for MUID. Caller picks (esp_random / get_rand_32 / etc).
uint32_t plat_rng() { return random(); }

void setup() {
  Serial.begin(115200);

  midi.setWriteFn(plat_write);
  midi.setNowFn(plat_now);
  midi.setMounted(true);
  midi.setAltSetting(1);   // 1 = MIDI 2.0 stream
  midi.begin();
  midi.enableJRHeartbeat(500);

  ci.setRngFn(plat_rng);
  static const uint8_t mfrId[3] = {0x7D, 0x00, 0x00};  // educational prefix
  ci.begin(mfrId, /*family*/ 0x0001, /*model*/ 0x0001, /*version*/ 0x00010000);
  ci.addPropertyStatic("DeviceInfo",
    "{\"manufacturer\":\"midi2_cpp\",\"model\":\"hello\"}");

  midi.onNoteOn([](uint8_t /*g*/, uint8_t ch, uint8_t n, uint16_t v,
                   uint8_t /*at*/, uint16_t /*ad*/) {
    Serial.print("NoteOn ch="); Serial.print(ch);
    Serial.print(" note=");      Serial.print(n);
    Serial.print(" vel=");       Serial.println(v);
  });
}

void loop() {
  // 4. Inbound UMP. Pump RX from the platform's USB MIDI callback into the lib.
  uint32_t in[16];
  size_t n = /* read up to 16 UMP words from your USB driver */ 0;
  if (n) midi.feedRx(in, n);

  midi.task();   // dispatches reassembled SysEx, fires heartbeat
}
```

The four hooks (`setWriteFn`, `feedRx`, `setNowFn`, `setMounted` + `setAltSetting`) are the entire platform contract. When an injection point is left unset the corresponding feature degrades safely (no transport, frozen MUID, no heartbeat).

## What you get

- USB MIDI 2.0 device, host, or both, depending on the board.
- 49 typed UMP callbacks: notes, CCs, RPN/NRPN, per-note expression, Flex Data, Stream messages.
- MIDI-CI out of the box: Discovery, Profile negotiation, Property Exchange (with Subscribe/Notify), Process Inquiry.
- Static configuration. No `malloc`, no `new`. Sized at compile time, fits a Cortex-M0+.
- Pay-as-you-go: only the modules called by the sketch end up in the binary.

## Three shapes

| Class | Role | Status |
|-------|------|--------|
| `m2device` | USB MIDI 2.0 device, board enumerates as a MIDI peripheral on a host (DAW, OS) | **available** |
| `m2host` | USB MIDI 2.0 host, board exposes a USB-A port and reads attached MIDI devices | **available** |
| `m2bridge` | Host + device, both ports active; route, upscale (MIDI 1.0 to 2.0), filter, transform | roadmap |

Same callback API across the three. `m2device` and `m2host` ship as reusable classes; the bridge pattern is demonstrated end to end in [`examples/adafruit-feather-rp2040-bridge-midi2`](examples/adafruit-feather-rp2040-bridge-midi2/) and will graduate to a reusable `m2bridge` class once the dual-stack glue stabilises.

## Boards

Validated on real hardware against forks and PRs maintained internally while the upstream merges are pending. The **Status** column names the override each test required. Concrete recipes ship under [`examples/`](examples/), one per role (device, host, bridge) and per board target.

| Board | MCU | Device | Host | Transport | Status |
|-------|-----|:-:|:-:|-----------|--------|
| **ESP32-S3 DevKitC-1** | ESP32-S3 | ✅ | - | TinyUSB | TinyUSB PR #3571, recipe in [`esp32-s3-devkitc-usb-midi2`](examples/esp32-s3-devkitc-usb-midi2) |
| **Waveshare ESP32-P4-WIFI6-DEV-KIT** | ESP32-P4 | ✅ | ✅ | TinyUSB | TinyUSB PR #3571 + mandatory `LP_SYS.usb_ctrl` PHY swap on the device side, recipe in [`esp32-p4-devkit-usb-midi2`](examples/esp32-p4-devkit-usb-midi2) (host + bridge variants pending) |
| T-Display S3 | ESP32-S3 | ✅ | ✅ | TinyUSB, ESP-NOW, BLE, UART, USB-OTG | TinyUSB PR #3571 |
| T-Display S3 AMOLED | ESP32-S3 | ✅ | ✅ | TinyUSB, ESP-NOW, BLE, UART, USB-OTG | TinyUSB PR #3571 |
| Teensy 4.1 | i.MX RT1062 | ✅ | ✅ | Native USB MIDI 2.0 (AS0 + AS1) | Teensy core fork (local) |
| Daisy Seed | STM32H750 | ✅ | - | STM32 HAL USB | libDaisy fork `feature/midi2-handler` |
| **Raspberry Pi Pico** | RP2040 | ✅ | - | TinyUSB | TinyUSB PR #3571, recipe in [`examples/rp2040-midi2`](examples/rp2040-midi2) |
| **Waveshare RP2040 Pi Zero** | RP2040 | ✅ | - | TinyUSB | TinyUSB PR #3571, recipe in [`examples/waveshare-rp2040-midi2`](examples/waveshare-rp2040-midi2) |
| **Adafruit Feather RP2040 USB Host** | RP2040 | ✅ | ✅ | TinyUSB, PIO-USB | TinyUSB PR #3571 + Pico-PIO-USB `675543b` (handshake delay fix), recipes in [`adafruit-feather-rp2040-host-midi2`](examples/adafruit-feather-rp2040-host-midi2) and [`adafruit-feather-rp2040-bridge-midi2`](examples/adafruit-feather-rp2040-bridge-midi2) |
| **Waveshare RP2350-USB-A** | RP2350 | ✅ | ✅ | TinyUSB, PIO-USB on GP12/GP13 | TinyUSB PR #3571 + Pico-PIO-USB `675543b` + R13 hardware mod for host mode (desolder the 1.5 kΩ pull-up on USB-A D+), recipes in [`waveshare-rp2350-usb-a-midi2`](examples/waveshare-rp2350-usb-a-midi2) (device) and [`waveshare-rp2350-usb-a-bridge-midi2`](examples/waveshare-rp2350-usb-a-bridge-midi2) (bridge) |
| Raspberry Pi Pico 2 | RP2350 | ✅ | ✅ | TinyUSB, PIO-USB | TinyUSB PR #3571 |
| ESP32-C6 | ESP32-C6 | ✅ | - | TinyUSB, BLE | TinyUSB PR #3571 |
| Nordic nRF52840 | nRF52840 | ✅ | - | TinyUSB, BLE | TinyUSB PR #3571 |
| Xiao SAMD21 | SAMD21 | ✅ | - | TinyUSB | TinyUSB PR #3571 |
| T-PicoC3 | RP2040 + ESP32-C3 | ✅ | - | TinyUSB | TinyUSB PR #3571 |

Four override sources cover everything: [TinyUSB PR #3571](https://github.com/hathach/tinyusb/pull/3571) (the bulk of the matrix, USB MIDI 2.0 device + host driver), [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) at SHA `675543b` (PR #186 "reduce handshake delay", required for MIDI 2.0 host enumeration over PIO-USB; predates the next tagged release), a local Teensy core fork (native USB MIDI 2.0 with AS0 + AS1 alt settings), and the [libDaisy](https://github.com/electro-smith/libDaisy) `feature/midi2-handler` fork. Each will retire from the Status column as it merges into its respective upstream.

### Coming soon

- Xiao Renesas RA4M1 (TinyUSB device, board on the bench)

## Install

### Arduino IDE

Library Manager: search `midi2_cpp`, click Install. The dependency on `midi2` is resolved automatically.

### PlatformIO

```ini
lib_deps =
  https://github.com/sauloverissimo/midi2_cpp.git#v0.1.0
```

Pin by tag for reproducibility. Pin by commit hash when a specific point in `main` is needed.

### ESP-IDF component

Drop `midi2_cpp/` into `components/`. The `CMakeLists.txt` is picked up automatically; `midi2` is vendored in-tree, so nothing else is required.

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    midi2_cpp
    GIT_REPOSITORY https://github.com/sauloverissimo/midi2_cpp.git
    GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(midi2_cpp)
```

### Git submodule

```bash
git submodule add https://github.com/sauloverissimo/midi2_cpp.git external/midi2_cpp
```

### Manual vendor

Download the repo. Add `src/` to includes. Compile `src/midi2.c`, `src/midi2_device.cpp`, and `src/midi2_ci.cpp` alongside the project. No external links required.

## API at a glance

```cpp
m2device midi;

midi.begin();

// Inbound: Arduino-style, just the channel, note, velocity.
midi.onNoteOn ([](uint8_t ch, uint8_t note, uint16_t vel) { /* ... */ });
midi.onNoteOff([](uint8_t ch, uint8_t note, uint16_t vel) { /* ... */ });
midi.onCC     ([](uint8_t ch, uint8_t idx, uint32_t val) { /* ... */ });
midi.onPitchBend([](uint8_t ch, uint32_t val) { /* ... */ });

// Outbound: same shape, no group prefix, 32-bit values.
midi.noteOn (0, 60, 0xC000);
midi.noteOff(0, 60);
midi.cc     (0, 7, 0x80000000);
midi.pitchBend(0, 0x80000000);

midi.task();
```

Async, callback-first, copy-paste-ready. Same shape as MIDI 1.0 Arduino libraries, with MIDI 2.0 resolution underneath.

**Need full spec fidelity?** Every callback and sender has a verbose form that exposes Group, MIDI 2.0 attribute type/data, and other Multi-Group Endpoint controls; see `midi2_device.h`. The simple and verbose forms share storage; the latest setter wins.

## Architecture

midi2_cpp is the platform layer of a 4-layer MIDI 2.0 stack:

```
┌──────────────────────────────────────┐
│ Sketch                               │  user code
├──────────────────────────────────────┤
│ midi2_cpp                            │  ***this library***
├──────────────────────────────────────┤
│ midi2                                │  portable C99 core (vendored)
├──────────────────────────────────────┤
│ TinyUSB / Native USB / PIO-USB / BLE │  transport (caller-wired)
└──────────────────────────────────────┘
```

The sketch touches the top. The rest is invisible until needed.

## What this library is not

The boundary is drawn so the wrapper stays focused. A few things deliberately do not belong here.

- **Not a low-level UMP parser.** That is `midi2`. midi2_cpp wraps it and adds C++ ergonomics; if a project wants zero-overhead C with no callbacks, linking `midi2` directly is the right move.
- **Not a synthesizer.** UMP arrives, callbacks fire, the sketch decides what to play. Sound generation is application territory.
- **Not a desktop library.** It targets MCU boards. It compiles on desktop for tests, but the API and memory model assume embedded constraints.
- **Not opinionated about transport.** TinyUSB, native USB (Teensy), PIO-USB (RP2350), STM32 HAL (Daisy), BLE: midi2_cpp does not bring any of them with it. The sketch wires whichever transport its platform already ships.

## Sponsor

You can sponsor midi2_cpp at [GitHub Sponsors](https://github.com/sponsors/sauloverissimo). Sponsorship funds boards for cross-platform validation, spec access, and continued maintenance.

## About

midi2_cpp is created and maintained by [Saulo Veríssimo](https://github.com/sauloverissimo). It is the C++17 sibling of midi2, originally extracted from USBMIDI2 work in arduino-esp32-uac and validated across the boards listed above.

## Specifications and trademarks

The MIDI 2.0 specifications referenced here are copyright of the [MIDI Association](https://midi.org) and available at https://midi.org/midi-2-0.

"MIDI" is a registered trademark of the MIDI Manufacturers Association (now MIDI Association). "MIDI 2.0", "MIDI-CI", and "UMP" are terms defined by the MIDI Association in the public specifications.

## License

MIT. Free for commercial and open-source use, in any context.

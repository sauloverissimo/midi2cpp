# midi2cpp

### MIDI 2.0 engine for embedded systems

![midi2cpp](https://raw.githubusercontent.com/sauloverissimo/midi2cpp/main/logo_midi2cpp.png)

*C++17, callback-first, static-by-default, depends on midi2, MIT.* From DIY to professional products.

[![release](https://img.shields.io/github/v/release/sauloverissimo/midi2cpp.svg)](https://github.com/sauloverissimo/midi2cpp/releases/latest)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://en.cppreference.com/cpp/compiler_support)
[![MIDI 2.0](https://img.shields.io/badge/MIDI-2.0-blueviolet.svg)](https://midi.org/specifications/midi-2-0-specifications)
[![depends: midi2](https://img.shields.io/badge/depends-midi2_%E2%89%A5_0.4.0-blueviolet.svg)](https://github.com/sauloverissimo/midi2)
[![Arduino](https://img.shields.io/badge/Arduino-IDE-00979D.svg)](https://www.arduino.cc/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Registry-FF7F00.svg)](https://registry.platformio.org/libraries/sauloverissimo/midi2cpp)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-E7352C.svg)](https://docs.espressif.com/projects/esp-idf/en/stable/)
[![ESP Component Registry](https://components.espressif.com/components/sauloverissimo/midi2cpp/badge.svg)](https://components.espressif.com/components/sauloverissimo/midi2cpp)
[![Pico SDK](https://img.shields.io/badge/Pico_SDK-2.0-C51A4A.svg)](https://github.com/raspberrypi/pico-sdk)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-064F8C.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

## The library

midi2cpp is the layer where a sketch meets the protocol. Plug a board into the laptop, write five lines of C++, flash, and the device appears on the bus as a USB MIDI 2.0 endpoint with full Capability Inquiry, Property Exchange, and 32-bit resolution.

Underneath, [midi2](https://github.com/sauloverissimo/midi2) (the portable C99 core) handles parsing, dispatch, and reassembly. midi2cpp adds the C++ ergonomics: callbacks, board glue, ready-made USB descriptors. The board does the talking; the sketch tells it what to say.

Single declared dependency: midi2. Transport, clock, and RNG are caller-supplied; no submodules.

## Contents

- [midi2cpp](#midi2cpp)
    - [MIDI 2.0 engine for embedded systems](#midi-20-engine-for-embedded-systems)
  - [The library](#the-library)
  - [Contents](#contents)
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

## Quickstart

`midi2cpp` is platform-agnostic: it parses, dispatches, and assembles UMP, and it leaves USB transport, clock, and entropy to the caller. The sketch wires four hooks to its platform's USB MIDI driver; the library does the rest.

```cpp
#include <midi2cpp.h>
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
    "{\"manufacturer\":\"midi2cpp\",\"model\":\"hello\"}");

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
- Static-by-default. The hot path is allocation-free; init-time `new` only inside `m2bridge` for the per-slot tables. Fits a Cortex-M0+.
- Pay-as-you-go: only the modules called by the sketch end up in the binary.

## Three shapes

| Class | Role | Status |
|-------|------|--------|
| `m2device` | USB MIDI 2.0 device, board enumerates as a MIDI peripheral on a host (DAW, OS) | **available** |
| `m2host` | USB MIDI 2.0 host, board exposes a USB-A port and reads attached MIDI devices | **available** |
| `m2bridge` | Host + device, both ports active; route, group-rewrite, dynamic FB names, MIDI 1.0 alt 0 uplift | **available** |

Same callback API across the three. `m2bridge` composes `m2device` + `m2ci` + `m2host` and adds a multi-slot Stream Discovery responder, raw UMP forward with per-slot group window rewrite, and an internal `ByteStreamConverter` per slot for MIDI 1.0 alt 0 upstream devices. Reference platform glue at [`examples/esp32-p4-devkit-bridge2-midi2`](examples/esp32-p4-devkit-bridge2-midi2/); the older [`examples/esp32-p4-devkit-bridge-midi2`](examples/esp32-p4-devkit-bridge-midi2/) and [`examples/adafruit-feather-rp2040-bridge-midi2`](examples/adafruit-feather-rp2040-bridge-midi2/) keep the same role with the slot table + responder carried inline, until they migrate to `m2bridge`.

## Boards

Validated on real hardware against forks and PRs maintained internally while the upstream merges are pending. midi2cpp is one of several integrations of the underlying [`midi2`](https://github.com/sauloverissimo/midi2) C99 core; concrete recipes for boards that use midi2cpp ship under [`examples/`](examples/), one per role (device, host, bridge). The **Status** column names the override each test required.

| Board | MCU | Device | Host | Bridge | Transport | Status |
|-------|-----|:-:|:-:|:-:|-----------|--------|
| **ESP32-S3 DevKitC-1** | ESP32-S3 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB PR #3571, recipe in [`esp32-s3-devkitc-usb-midi2`](examples/esp32-s3-devkitc-usb-midi2) |
| **Arduino Nano ESP32** | ESP32-S3 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB PR #3571, recipe in [`arduino-nano-esp32-midi2`](examples/arduino-nano-esp32-midi2) |
| **Waveshare ESP32-P4-WIFI6-DEV-KIT** | ESP32-P4 | ✅ | ✅ | ✅ | ![experimental](https://img.shields.io/badge/-experimental-yellow.svg) TinyUSB | TinyUSB `experiment/midi-coexistence` branch (alt-walk bcdMSC defer for MIDI 1.0 + 2.0 host coexistence) on top of PR #3571 + mandatory `LP_SYS.usb_ctrl` PHY swap on the device side, recipes in [`esp32-p4-devkit-usb-midi2`](examples/esp32-p4-devkit-usb-midi2) (device, INT PHY, OTG_FS), [`esp32-p4-devkit-host-midi2`](examples/esp32-p4-devkit-host-midi2) (host, UTMI PHY, OTG_HS) and [`esp32-p4-devkit-bridge-midi2`](examples/esp32-p4-devkit-bridge-midi2) (dual-stack bridge, inline glue) and [`esp32-p4-devkit-bridge2-midi2`](examples/esp32-p4-devkit-bridge2-midi2) (same role on top of `m2bridge`, PID 0x4095) |
| **LilyGo T-Display S3** | ESP32-S3 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB PR #3571, recipe in [`t-display-s3-midi2`](examples/t-display-s3-midi2) (Tier A receiver with on-board ST7789 piano roll). Hardware validated 2026-05-01: enumerates `cafe:4094` as `TDisplayS3`, `Group 1 (Main)` visible to ALSA, NoteOn/Off lights piano keys live |
| T-Display S3 AMOLED | ESP32-S3 | ✅ | ✅ | - | - | TinyUSB PR #3571 |
| Teensy 4.1 | i.MX RT1062 | ✅ | ✅ | - | - | direct consumer of midi2 (Teensy core fork, native USB MIDI 2.0 with AS0 + AS1) |
| **Raspberry Pi Pico** | RP2040 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB PR #3571, recipe in [`examples/rp2040-midi2`](examples/rp2040-midi2) |
| **Waveshare RP2040 Pi Zero** | RP2040 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB PR #3571, recipe in [`examples/waveshare-rp2040-midi2`](examples/waveshare-rp2040-midi2) |
| **Adafruit Feather RP2040 USB Host** | RP2040 | ✅ | ✅ | ✅ | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB, PIO-USB | TinyUSB PR #3571 + Pico-PIO-USB `675543b` (handshake delay fix), recipes in [`adafruit-feather-rp2040-host-midi2`](examples/adafruit-feather-rp2040-host-midi2) and [`adafruit-feather-rp2040-bridge-midi2`](examples/adafruit-feather-rp2040-bridge-midi2) |
| **RP2040 Pro Micro (Tenstar Robot)** | RP2040 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB PR #3571, deterministic UMP emitter for Windows MIDI Services testing in [`rp2040-promicro-ump-test-bench`](examples/rp2040-promicro-ump-test-bench) |
| **Waveshare RP2350-USB-A** | RP2350 | ✅ | ✅ | ✅ | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB, PIO-USB on GP12/GP13 | TinyUSB PR #3571 + Pico-PIO-USB `675543b` + R13 hardware mod for host mode (desolder the 1.5 kΩ pull-up on USB-A D+), recipes in [`waveshare-rp2350-usb-a-midi2`](examples/waveshare-rp2350-usb-a-midi2) (device) and [`waveshare-rp2350-usb-a-bridge-midi2`](examples/waveshare-rp2350-usb-a-bridge-midi2) (bridge) |
| **SparkFun Pro Micro RP2350** | RP2350 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB PR #3571, recipe in [`sparkfun-promicro-rp2350-midi2`](examples/sparkfun-promicro-rp2350-midi2) |
| Raspberry Pi Pico 2 | RP2350 | ✅ | ✅ | - | - | TinyUSB PR #3571 |
| **ESP32-C6-DevKitC-1** | ESP32-C6 | ![WIP](https://img.shields.io/badge/-WIP-orange.svg) | - | - | BLE-MIDI 1.0 + ESP-NOW | ![WIP](https://img.shields.io/badge/-work_in_progress-orange.svg) ESP32_Host_MIDI v6.0.x (BLE GATT visibility regressions still being iterated; the chip has no USB-OTG so the wireless transports are the only path here). Recipe in [`esp32-c6-devkitc-multi-midi2`](examples/esp32-c6-devkitc-multi-midi2) (Tier B wireless, no PID consumed). |
| **nRF52840 Pro Micro (Nice!Nano class)** | nRF52840 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB native CMake build (PR #3571 fork via FetchContent + `hw/bsp/family_support.cmake`, BSP `feather_nrf52840_express` upstream), recipe in [`nrf52840-promicro-midi2`](examples/nrf52840-promicro-midi2) (Tier B). Hardware validated 2026-04-30 on Nice!Nano: enumerates `cafe:40F1`, `Group 1 (Main)` visible to ALSA, Per-Note PB vibrato + chromatic walk + RPN/NRPN streaming live |
| **Seeed XIAO SAMD21** | SAMD21 | ✅ | - | - | ![override](https://img.shields.io/badge/-override-blueviolet.svg) TinyUSB | TinyUSB native CMake build (PR #3571 fork via FetchContent + `hw/bsp/family_support.cmake`, BSP `seeeduino_xiao` upstream), recipe in [`xiao-samd21-midi2`](examples/xiao-samd21-midi2) (Tier C). Hardware validated 2026-04-30: enumerates `cafe:40F0`, `Group 1 (Main)` visible to ALSA, chromatic walk + CC sweep streaming live |
| T-PicoC3 | RP2040 + ESP32-C3 | ✅ | - | - | - | TinyUSB PR #3571 |

Three override sources cover everything: [TinyUSB PR #3571](https://github.com/hathach/tinyusb/pull/3571) (the bulk of the matrix, USB MIDI 2.0 device + host driver), [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) at SHA `675543b` (PR #186 "reduce handshake delay", required for MIDI 2.0 host enumeration over PIO-USB; predates the next tagged release), and a local Teensy core fork (native USB MIDI 2.0 with AS0 + AS1 alt settings). Each will retire from the Status column as it merges into its respective upstream.

### Recipes by build system

20 recipes ship under [`examples/`](examples/), grouped by build path:

| Build system | Count | Recipes |
|---|:-:|---|
| **Pico SDK** | 8 | [`rp2040-midi2`](examples/rp2040-midi2), [`waveshare-rp2040-midi2`](examples/waveshare-rp2040-midi2), [`sparkfun-promicro-rp2350-midi2`](examples/sparkfun-promicro-rp2350-midi2), [`waveshare-rp2350-usb-a-midi2`](examples/waveshare-rp2350-usb-a-midi2), [`waveshare-rp2350-usb-a-bridge-midi2`](examples/waveshare-rp2350-usb-a-bridge-midi2), [`adafruit-feather-rp2040-host-midi2`](examples/adafruit-feather-rp2040-host-midi2), [`adafruit-feather-rp2040-bridge-midi2`](examples/adafruit-feather-rp2040-bridge-midi2), [`rp2040-promicro-ump-test-bench`](examples/rp2040-promicro-ump-test-bench) |
| **ESP-IDF** | 7 | [`arduino-nano-esp32-midi2`](examples/arduino-nano-esp32-midi2), [`esp32-s3-devkitc-usb-midi2`](examples/esp32-s3-devkitc-usb-midi2), [`esp32-p4-devkit-usb-midi2`](examples/esp32-p4-devkit-usb-midi2), [`esp32-p4-devkit-host-midi2`](examples/esp32-p4-devkit-host-midi2), [`esp32-p4-devkit-bridge-midi2`](examples/esp32-p4-devkit-bridge-midi2), [`esp32-p4-devkit-bridge2-midi2`](examples/esp32-p4-devkit-bridge2-midi2), [`t-display-s3-midi2`](examples/t-display-s3-midi2) |
| **PlatformIO + ESP32_Host_MIDI** | 3 | [`esp32-c6-devkitc-multi-midi2`](examples/esp32-c6-devkitc-multi-midi2), [`esp32-s3-devkitc-host-midi2`](examples/esp32-s3-devkitc-host-midi2), [`t-display-s3-shield-host-midi2`](examples/t-display-s3-shield-host-midi2) |
| **TinyUSB native CMake** | 2 | [`xiao-samd21-midi2`](examples/xiao-samd21-midi2), [`nrf52840-promicro-midi2`](examples/nrf52840-promicro-midi2) |

By role: 10 device, 4 host, 4 bridge, 1 multi-transport (BLE + ESP-NOW, no USB PID), 1 deterministic UMP test bench.

### Coming soon

- Xiao Renesas RA4M1 (TinyUSB device, board on the bench)

## Install

### Arduino IDE

Listed on the [Arduino Library Manager](https://github.com/arduino/library-registry). The IDE install path: search the manager, click Install. The dependency on `midi2` is resolved automatically.

Manual install (mirror, or while the manager index is propagating):

```bash
git clone https://github.com/sauloverissimo/midi2cpp.git ~/Arduino/libraries/midi2cpp
```

Then install `midi2` via Library Manager (search `midi2`, click Install).

### PlatformIO

Published on the [PlatformIO Registry](https://registry.platformio.org/libraries/sauloverissimo/midi2cpp):

```ini
lib_deps = sauloverissimo/midi2cpp @ ^0.4.0
```

Or pin by git tag:

```ini
lib_deps =
  https://github.com/sauloverissimo/midi2cpp.git#v0.4.0
```

Either way, `midi2` is resolved transitively via the manifest declaration in `library.json`.

### ESP-IDF component

Published on the [ESP Component Registry](https://components.espressif.com/components/sauloverissimo/midi2cpp). Two install paths, depending on whether midi2cpp comes from the registry or lives inside the project tree:

**Via the Component Manager** (recommended):

```yaml
# main/idf_component.yml
dependencies:
  idf: ">=5.0"
  sauloverissimo/midi2cpp: ">=0.4.0"
```

`midi2` is pulled transitively through `midi2cpp`'s manifest. `idf.py reconfigure` drops both into `managed_components/`.

**As a local component** (vendoring pattern, useful when iterating on the wrapper):

```bash
# from your IDF project root
git clone https://github.com/sauloverissimo/midi2cpp.git components/midi2cpp
```

Then declare `midi2` in `main/idf_component.yml`:

```yaml
dependencies:
  idf: ">=5.0"
  sauloverissimo/midi2: ">=0.4.0"
```

`main/CMakeLists.txt` lists `midi2cpp` in its `idf_component_register(... REQUIRES midi2cpp ...)` block. The seven ESP-IDF recipes under [`examples/`](examples/) ship working templates for device, host and bridge roles.

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    midi2cpp
    GIT_REPOSITORY https://github.com/sauloverissimo/midi2cpp.git
    GIT_TAG        v0.4.0
)
FetchContent_MakeAvailable(midi2cpp)
```

`midi2cpp` cascades the dependency on `midi2` to the parent project: `find_package(midi2 0.4.0 CONFIG)` is tried first, falling back to `FetchContent_Declare(midi2 GIT_TAG v0.4.0)` if no install is found.

### Git submodule

```bash
git submodule add https://github.com/sauloverissimo/midi2cpp.git external/midi2cpp
```

### Manual vendor

Download the [midi2cpp](https://github.com/sauloverissimo/midi2cpp) and [midi2](https://github.com/sauloverissimo/midi2) repositories side by side. Add `midi2/dist/` and `midi2cpp/src/` to includes. Compile `midi2/dist/midi2.c`, `midi2cpp/src/midi2_device.cpp`, `midi2cpp/src/midi2_ci.cpp`, and the host/bridge `.cpp` files you need alongside the project. No package manager required at build time, but the two repos must travel together.

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

midi2cpp: platform layer of a 4-layer MIDI 2.0 stack:

![midi2cpp](https://raw.githubusercontent.com/sauloverissimo/midi2cpp/main/architecture.png)

The sketch touches the top. The rest is invisible until needed.

## What this library is not

The boundary is drawn so the wrapper stays focused. A few things deliberately do not belong here.

- **Not a low-level UMP parser.** That is `midi2`. midi2cpp wraps it and adds C++ ergonomics; if a project wants zero-overhead C with no callbacks, linking `midi2` directly is the right move.
- **Not a synthesizer.** UMP arrives, callbacks fire, the sketch decides what to play. Sound generation is application territory.
- **Not a desktop library.** It targets MCU boards. It compiles on desktop for tests, but the API and memory model assume embedded constraints.
- **Not opinionated about transport.** TinyUSB, native USB (Teensy), PIO-USB (RP2350), STM32 HAL (Daisy), BLE: midi2cpp does not bring any of them with it. The sketch wires whichever transport its platform already ships.

## Sponsor

You can sponsor midi2cpp at [GitHub Sponsors](https://github.com/sponsors/sauloverissimo). Sponsorship funds boards for cross-platform validation, spec access, and continued maintenance.

## About

midi2cpp is created and maintained by [Saulo Veríssimo](https://github.com/sauloverissimo). It is the C++17 sibling of midi2, originally extracted from USBMIDI2 work in arduino-esp32-uac and validated across the boards listed above.

## Specifications and trademarks

The MIDI 2.0 specifications referenced here are copyright of the [MIDI Association](https://midi.org) and available at https://midi.org/midi-2-0.

"MIDI" is a registered trademark of the MIDI Manufacturers Association (now MIDI Association). "MIDI 2.0", "MIDI-CI", and "UMP" are terms defined by the MIDI Association in the public specifications.

## License

MIT. Free for commercial and open-source use, in any context.

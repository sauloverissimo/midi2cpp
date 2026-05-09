# Changelog

All notable changes to `midi2cpp` are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html),
mirrored from the upstream midi2 C99 policy.

## [0.2.0]

Single source of truth for the MIDI 2.0 stack: midi2cpp no longer
vendors the C99 core and is published as a regular Arduino /
PlatformIO library that depends on midi2 explicitly. Every recipe
under `examples/` was migrated to pull midi2 externally through the
build system that fits its host (FetchContent for Pico SDK + TinyUSB
native CMake, IDF Component Manager for ESP-IDF, lib_deps for
PlatformIO).

This is a breaking release. Consumers that previously vendored
`midi2cpp/src/midi2.{h,c}` directly will break; the migration path
is documented in the manifest below and in the per-build-system
patterns shipped under `examples/`.

### Breaking

- **Vendored `src/midi2.{h,c}` removed.** midi2cpp now declares
  midi2 as an external dependency:
  - `library.properties` carries `depends=midi2 (>=0.3.4)`. Arduino
    Library Manager auto-installs midi2 when a sketch includes
    midi2cpp.
  - `library.json` carries `dependencies."sauloverissimo/midi2":
    "^0.3.4"`. PlatformIO resolves midi2 from its registry.
  - The root `CMakeLists.txt` exposes a three-layer fallback at the
    top (`if(NOT TARGET midi2)` -> `find_package(midi2 0.3.4 CONFIG)`
    -> `FetchContent_Declare(midi2 GIT_TAG v0.3.4)`), then links
    midi2cpp `PUBLIC midi2::midi2` so downstream targets see the
    C99 core transitively.

### Added

- **`midi2::Bridge` (alias `m2bridge`)**: composes Device + CI + Host
  with a multi-Function-Block topology, a per-slot group rewrite
  window, dynamic FB names sourced from upstream Endpoint Names, and
  a USB-MIDI 1.0 byte-stream uplift path (`feedHostMidi1Bytes`) for
  legacy upstream devices that arrive on alt 0. Slot lifecycle via
  `slotSetActive(idx, active, alt)`. Reusable across bridge recipes;
  the multi-FB Stream Discovery responder lives inside the class so
  each new bridge recipe gets it for free.
- **`tests/test_midi2_bridge.cpp`**: 11 host-side sub-tests covering
  m2bridge construct/destruct heap balance (50x cycle stress), topology
  setter bounds and post-`begin()` lock, group rewrite formula on
  slots 0/1/3, out-of-range slot rejection, and the USB-MIDI 1.0
  byte-stream uplift path. Compiles and runs clean under
  `-fsanitize=address,undefined`.
- `architecture.png` referenced from the README, replacing the
  previous inline ASCII layer diagram.
- **CMake entry surface for downstream consumers**: the root
  `CMakeLists.txt` follows the same `find_package` -> `FetchContent`
  fallback pattern that midi2 itself ships. Subprojects pulling
  midi2cpp via `add_subdirectory` or `FetchContent` skip the
  `find_package` step (`if(NOT TARGET midi2)` guard).

### Changed

- **README tagline** drops the `zero-allocation` claim. midi2cpp
  allocates in two narrow places (`m2bridge::begin()` slot tables and
  `std::function` callback storage), so the wrapper is now described
  as `static-by-default`. The C99 core (midi2) remains strictly
  zero-allocation. Same shift applied to the logo and to the
  `.intern/decisoes.md` design heritage notes.
- **README "Manual vendor" path** rewritten: pre-v0.2 builds vendored
  a single `midi2cpp/src/midi2.{h,c}` copy; today the consumer
  downloads both repositories side by side and adds `midi2/dist/`
  plus `midi2cpp/src/` to its include path.
- **`paragraph` in `library.properties`** rewritten: drops
  comparisons with other libraries, focuses on what midi2cpp itself
  ships and the embedded targets validated.

### Examples / Recipes

#### Migrated to depend on midi2 externally (all 20 recipes)

| Build system | Mechanism | Recipes |
|---|---|---|
| Pico SDK | `FetchContent_Declare(midi2 GIT_TAG v0.3.4)` plus `target_link_libraries(midi2cpp PUBLIC midi2::midi2)` | `rp2040-midi2`, `waveshare-rp2040-midi2`, `sparkfun-promicro-rp2350-midi2`, `waveshare-rp2350-usb-a-midi2`, `waveshare-rp2350-usb-a-bridge-midi2`, `adafruit-feather-rp2040-host-midi2`, `adafruit-feather-rp2040-bridge-midi2`, `rp2040-promicro-ump-test-bench` |
| TinyUSB native CMake | same FetchContent pattern as Pico SDK | `xiao-samd21-midi2`, `nrf52840-promicro-midi2` |
| ESP-IDF | `idf_component.yml` declares `midi2: { git: ..., version: ">=0.3.4" }` and `idf_component_register` lists `midi2` in `REQUIRES` | `arduino-nano-esp32-midi2`, `esp32-s3-devkitc-usb-midi2`, `esp32-p4-devkit-usb-midi2`, `esp32-p4-devkit-host-midi2`, `esp32-p4-devkit-bridge-midi2`, `esp32-p4-devkit-bridge2-midi2`, `t-display-s3-midi2` |
| PlatformIO + ESP32_Host_MIDI | `lib_deps += sauloverissimo/midi2 @ ^0.3.4` | `esp32-c6-devkitc-multi-midi2`, `esp32-s3-devkitc-host-midi2`, `t-display-s3-shield-host-midi2` |

Each recipe drops the `${MIDI2CPP_ROOT}/src/midi2.c` (or `midi2_c99`
helper library) from its source list. Other midi2cpp sources
(`midi2_device.cpp`, `midi2_ci.cpp`, `midi2_host.cpp`,
`midi2_bridge.cpp`) keep being compiled inline from the parent tree
via `${MIDI2CPP_ROOT}/src` until the host helper-library shape is
finalised in a future cycle.

#### New recipes since v0.1.0

- `arduino-nano-esp32-midi2`, Arduino Nano ESP32 (ESP32-S3-MINI-1,
  PID 0x4093). Full Showcase mirroring `esp32-s3-devkitc-usb-midi2`;
  single GPIO LED on D13 / GPIO48 instead of WS2812.
- `xiao-samd21-midi2`, Seeed Studio XIAO SAMD21 (ATSAMD21G18A, PID
  0x40F0). Tier C minimal core; first recipe to use the TinyUSB
  native CMake build system path. Hardware validated: ALSA `Group 1
  (Main)`, chromatic walk + 32-bit CC #74 sweep streaming. Final
  size: text 34884 / 256K flash (13%), bss 9832 / 32K SRAM (30%).
- `nrf52840-promicro-midi2`, nRF52840 Pro Micro / Nice!Nano class
  (PID 0x40F1). Tier B subset: Per-Note Pitch Bend vibrato +
  chromatic walk + RPN / NRPN / RelRPN / RelNRPN burst. Same TinyUSB
  native CMake build path as the SAMD21 recipe. Hardware validated on
  Nice!Nano. Final size: text 38832 / 1 MB flash (3.7%), bss 2526 /
  256 KB SRAM (1%).
- `esp32-p4-devkit-bridge2-midi2`, ESP32-P4 dual-stack bridge (PID
  0x4095) built on top of `m2bridge`. Carries the same multi-FB
  topology as `esp32-p4-devkit-bridge-midi2` but consumes the
  reusable Bridge class instead of an inline slot table + Stream
  Discovery responder.

## [0.1.0]

First public release. C++17 Arduino-style wrapper for MIDI 2.0 on
embedded devices, layered over the portable [midi2 C99](https://github.com/sauloverissimo/midi2)
library (vendored stb-style at `src/midi2.{h,c}` from v0.3.0+).

### `midi2::Device` — UMP transport (M2-104)

- Lifecycle: `begin()` initialises the dispatcher and proc; the caller
  owns the platform USB stack lifecycle. `task()` runs the JR Timestamp
  heartbeat. `isMounted()`, `altSetting()` accessors.
- All 8 message types covered:
  - **MT 0x0 Utility**: `sendNoop`, `sendJRClock`, `sendJRTimestamp`,
    `sendDctpq`, `sendDeltaClockstamp`. Defensive heartbeat via
    `enableJRHeartbeat(intervalMs)` (default 500 ms).
  - **MT 0x1 System** (10 wrappers + generic escape): tune request,
    clock, start/continue/stop, active sensing, system reset, MTC, song
    select, song position, plus `sendSystemGeneric`.
  - **MT 0x2 MIDI 1.0 CV** (7 wrappers + `ByteStreamConverter`): note
    on/off, CC, program, pitch bend, channel pressure, poly pressure.
    Inbound MT 0x2 can be auto-upscaled to MT 0x4 callbacks via
    `setUpscaleMt2(true)`.
  - **MT 0x3 SysEx7**: `sendSysEx7(group, data, len)` with automatic
    fragmentation; reassembly via `onSysEx7` (512 byte buffer).
  - **MT 0x4 MIDI 2.0 CV** (15 wrappers): note on/off (with attribute
    type+data), CC, program (with optional bank), pitch bend, channel
    pressure, poly pressure, RPN, NRPN, Relative RPN/NRPN, per-note
    pitch bend, registered/assignable per-note controllers, per-note
    management.
  - **MT 0x5 SysEx8**: `sendSysEx8(group, streamId, data, len)` with
    automatic fragmentation; reassembly via `onSysEx8`.
  - **MT 0xD Flex Data** (6 wrappers): tempo, time signature,
    metronome, key signature, `sendChordName(ChordDescriptor)` (20-field
    struct), flex text (refuses payloads > 12 bytes; multi-UMP
    fragmentation deferred).
  - **MT 0xF UMP Stream** (6 wrappers): device identity (`mfrId[3]`
    MSB-first), endpoint name update, product instance ID update, FB
    name update, start/end of clip (endpoint-wide, no group field).
- 49 inbound dispatch callbacks via `onXxx(std::function)` setters.
- All `sendXxx` return `bool`: `true` = emitted, `false` = dropped on
  back-pressure or refused at API boundary.
- Bit Scaling (M2-115): `scaleUp7to16`, `scaleUp7to32`, `scaleUp14to32`,
  `scaleDown16to7`, `scaleDown32to7`, `scaleDown32to14` static methods,
  exhaustive roundtrip tested.
- Helpers: `setUmpGroup`, `setGroupRemap`, `downgradeMt4ToMt2`,
  `cableEventToUmp`, `setUpscaleMt2`.
- `ByteStreamConverter` inner class: MIDI 1.0 DIN-5 byte stream → UMP
  MT 0x2 / MT 0x3 with running status and SysEx accumulation.

### `midi2::CI` — MIDI-CI v1.2 (M2-101)

- Lifecycle: `begin(mfrId[3], family, model, version, ciCat=0x1C)`
  enables Profile + PE + PI by default.
- MUID: `muid()`, `regenerateMuid()`. Auto-regeneration on collision
  and on Invalidate MUID via the caller-supplied RNG (`CI::setRngFn`).
- Convenience responder (M2-101 Appendix E) auto-replies to Discovery,
  Profile Inquiry, PE Capability, PE Get; opt-out via setting custom
  callbacks. NAK-on-unknown enabled by default.
- Discovery: `sendDiscoveryInquiry()` (Initiator), `onDiscovery` /
  `onDiscoveryReply` notifications, `onInvalidateMuid`.
- Profile (M2-101 §7): `addProfile`, `removeProfile`, callbacks for
  `onProfileInquiry`, `onProfileEnable`, `onProfileDisable`,
  `onProfileAdded`, `onProfileRemoved`, `onProfileDetailsInquiry`,
  `onProfileSpecificData`. Storage tunable via `MIDI2CPP_MAX_PROFILES`
  (default 8).
- Property Exchange (M2-101 §8, M2-103, M2-105):
  - Registry: `addProperty(name, getter, setter=nullptr)` (read-only by
    default), `addPropertyStatic(name, value)`, `removeProperty`.
    Storage tunable via `MIDI2CPP_MAX_PROPERTIES` (default 8).
  - Subscribe / Notify state machine:
    `setPropertySubscribable(name, true)`, `notifyPropertyChanged(name)`
    for fan-out, `subscriberCount()`. Subscriber registry tunable via
    `MIDI2CPP_MAX_SUBSCRIBERS` (default 4).
  - PE callbacks deliver raw bytes (header + body) instead of
    NUL-terminated strings, to avoid silent truncation of large JSON
    payloads.
- Process Inquiry (M2-101 §9 + Appendix F): `setMidiReport`,
  `onPICapability`, `onMidiReportInquiry`.

### `midi2::Host` — USB MIDI 2.0 host shape

- Reactive multi-device host (`MIDI2CPP_HOST_MAX_DEVICES`, default 4).
  Caller wires `tuh_midi2_*` (or platform-equivalent) into
  `notifyDeviceMounted/Unmounted`, `feedRx(idx, words, count)` and
  `setWriteFn(idx, words, count)`.
- Per-device `DeviceIdentity` populated as UMP Stream Endpoint
  Discovery + MIDI-CI Discovery replies arrive: `umpVerMajor/Minor`,
  `supportsMidi1Protocol`, `supportsMidi2Protocol`, `numFunctionBlocks`,
  `manufacturerId[3]`, `familyId`, `modelId`, `version`, `endpointName`,
  `productInstanceId`, `bcdMSC`, `altSettingActive`, `ciMuid`,
  `ciDiscoveryPending` / `ciDiscoveryRequestId` / `ciDiscoverySentMs`
  for Initiator timeout tracking.
- CI Initiator role: host owns its own MUID (seeded via `setRngFn`,
  regeneratable on collision), sends Discovery Inquiry, matches replies
  by request id.
- Auto-discovery on mount (default ON): UMP Stream Endpoint Discovery
  + MIDI-CI Discovery Inquiry fire automatically when
  `notifyDeviceMounted` is called.
- 22 inbound dispatch callbacks, all `idx`-prefixed (NoteOn/Off, CC,
  Pitch Bend, Channel/Poly Pressure, Per-Note Pitch Bend, Per-Note
  controllers, Program, SysEx7/8, Flex Data, JR Timestamp, plus
  identity-update lifecycle).
- Group remap per device: `setInboundGroupRemap(idx, map[16])` for the
  bridge use case downstream of a multi-group endpoint.
- Threading model documented: `feedRx` and `notifyDeviceMounted/Unmounted`
  are task-context only; ISR-context platforms must defer via their own
  queue.

### Bridge & internals

- `Device` ↔ `CI` bridge via friend-only methods (`_setCiSysExHook`,
  `_ciWriteFnContext`); user-facing `onSysEx7` keeps working alongside
  CI's SysEx routing.
- Two-hop dispatch context (`proc.context = &dispatch`,
  `dispatch.context = DeviceState*`) lets `midi2_dispatch_feed` and
  reassembled SysEx callbacks coexist without upstream patches.
- `~CI()` clears Device's CI hook to avoid use-after-free if CI dies
  before Device.
- `removeProperty` mirrors midi2_ci's left-shift on the parallel
  `pe_getters[]` / `pe_setters[]` arrays (alignment invariant).

### Platform contract (5 caller-wired hooks)

- `Device::setWriteFn(WriteFn)` — outbound UMP. Library invokes the
  caller's function for every `sendXxx` and the JR heartbeat.
- `Device::feedRx(const uint32_t* words, size_t count)` — inbound UMP.
  Caller pumps RX into the library; chunks transparently to the
  upstream `uint8_t word_count` limit of `midi2_proc_feed`.
- `Device::setNowFn(NowFn)` — monotonic ms clock for the JR heartbeat.
  When unset, the heartbeat never fires (link-safe on bare hosts).
- `Device::setMounted(bool)` / `Device::setAltSetting(uint8_t)` — caller
  informs USB enumeration state.
- `CI::setRngFn(RngFn)` — caller-supplied entropy source. When unset,
  MUID stays at the value seeded in `begin()`.

`Host` follows the same pattern with idx-prefixed equivalents
(`Host::setWriteFn`, `feedRx(idx, …)`, `notifyDeviceMounted(idx, …)`,
`notifyDeviceUnmounted(idx)`, `setNowFn`, `setRngFn`).

### Platform-agnostic library

- Removed every `#if defined(ARDUINO) || defined(PICO_PLATFORM) ||
  defined(ESP_PLATFORM)` block from `midi2_device.{h,cpp}` and the
  platform-conditional RNG `#if` chain from `midi2_ci.cpp`. The library
  no longer pulls `<Arduino.h>`, `pico/time.h`, `esp_timer.h`, or any
  USB stack header.
- Removed the `MIDI2CPP_TEST_MODE` build option. Tests now consume the
  same public hooks platforms wire. One contract, one code path.
- `Device::begin()` no longer claims to call `tusb_init` internally. It
  initialises the library's own dispatcher and returns; the caller owns
  the platform USB stack lifecycle.
- `Device::task()` drops the commented `tud_task` stubs.
- C++17 floor enforced via `static_assert(__cplusplus >= 201703L)` in
  `midi2cpp.h`.
- `lib/tinyusb` git submodule and the `.gitmodules` entry removed. The
  library has zero external dependencies: midi2 C99 stays vendored,
  every USB stack and clock and RNG source is caller-wired. `git clone`
  is the install — no `--recurse-submodules`, no half-initialised state.

### Examples / Recipes

12 platform recipes covering RP2040, RP2350, ESP32-S3 and ESP32-P4
boards, each with the platform-specific glue + a showcase main:

- `rp2040-midi2` — Raspberry Pi Pico, first concrete platform recipe
- `waveshare-rp2040-midi2` — Waveshare RP2040 Pi Zero
- `sparkfun-promicro-rp2350-midi2` — SparkFun Pro Micro RP2350
- `ump-test-bench-rp2040` — RP2040 Pro Micro (Tenstar Robot),
  deterministic 101-entry UMP catalog emitter for Windows MIDI Services
  consumer-side testing
- `esp32-s3-devkitc-usb-midi2` — ESP32-S3 DevKitC-1 (PID 0x4090)
- `esp32-p4-devkit-usb-midi2` — Waveshare ESP32-P4-WIFI6-DEV-KIT device
  (PID 0x4091, INT PHY OTG_FS, mandatory `LP_SYS.usb_ctrl` swap)
- `esp32-p4-devkit-host-midi2` — same kit as host (UTMI PHY OTG_HS)
- `esp32-p4-devkit-bridge-midi2` — same kit as dual-stack bridge (PID
  0x4092), validated with simultaneous MIDI 1.0 (Arturia MiniLab 25) +
  MIDI 2.0 (ESP32-S3) host coexistence via the experimental TinyUSB
  alt-walk bcdMSC defer
- `adafruit-feather-rp2040-host-midi2` — Adafruit Feather RP2040 USB
  Host with SSD1306 OLED, MIDI 2.0 host over PIO-USB
- `adafruit-feather-rp2040-bridge-midi2` — same Feather as dual-stack
  bridge (USB-C device + USB-A host)
- `waveshare-rp2350-usb-a-midi2` — Waveshare RP2350-USB-A device
  (requires R13 hardware mod)
- `waveshare-rp2350-usb-a-bridge-midi2` — same kit as dual-stack bridge

Each recipe ships with: TinyUSB PR #3571 fork bootstrap script (ESP-IDF
recipes use `idf/scripts/fetch_tinyusb.sh`; Pico SDK recipes use
CMake FetchContent), USB descriptors with PID, board-specific platform
glue, and a README with build, flash and validation instructions.

### Build, packaging, tests

- Triple build: Arduino `library.properties`, PlatformIO `library.json`,
  CMake.
- midi2 C99 v0.3.0 vendored stb-style at `src/midi2.{h,c}`.
- 6 host-side test binaries (`test_midi2_device`, `test_midi2_ci`,
  `test_midi2_host`, `test_midi2_conversion`, `test_midi2_flex`,
  `test_midi2_scaling`) with 70+ assertions, all green under `-Wall
  -Wextra -Wpedantic` on gcc + clang.
- GitHub Actions: host-tests matrix (Ubuntu / macOS), Pico SDK example
  build, Arduino compile guarded by `.ino` presence.

### Documentation

- README with quickstart, four-hooks contract, Boards table covering
  the 12 recipes plus other reference platforms (Teensy core fork as
  direct consumer), Architecture diagram of the 4-layer stack,
  Three-shapes table (`m2device` / `m2host` / `m2bridge` planned),
  Install paths for Arduino IDE / PlatformIO / ESP-IDF / CMake
  FetchContent / git submodule / manual vendor.
- Override status badges on Boards table rows: `override` (blueviolet)
  for unmerged-PR/fork dependencies, `experimental` (yellow) for
  research branches on top of an override.

### Known limitations

- AVR Uno (2 KB RAM) is out of scope.
- `setMaxSysexSize` not exposed (midi2 C99 lacks the setter upstream).
- 5 Initiator-role senders (`sendEndpointInfoInquiry/Reply`, `sendAck`,
  `sendProfileDetailsReply`, `sendProfileSpecificData`) deferred — the
  convenience responder covers the common Receiver flows.
- `sendFlexText` does not yet fragment payloads > 12 bytes.
- MT 0x2 named senders cover the simple case; UMP → DIN-5 byte stream
  bridge deferred.
- `m2bridge` reusable class (composition of host + device with an UMP
  router) is the headline target for the v0.2 cycle; today the bridge
  pattern lives per-recipe in the three bridge examples.

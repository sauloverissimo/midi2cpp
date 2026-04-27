# Changelog

All notable changes to `midi2_cpp` are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html),
mirrored from the upstream midi2 C99 policy.

## [Unreleased]

### Changed — platform-agnostic library
- Removed every `#if defined(ARDUINO) || defined(PICO_PLATFORM) ||
  defined(ESP_PLATFORM)` block from `midi2_device.{h,cpp}` and the
  platform-conditional RNG `#if` chain from `midi2_ci.cpp`. The library
  no longer pulls `<Arduino.h>`, `pico/time.h`, `esp_timer.h`, or any
  USB stack header.
- Removed the `MIDI2_CPP_TEST_MODE` build option. Tests now consume the
  same public hooks platforms wire (`setWriteFn`, `setNowFn`, `feedRx`,
  `setMounted`, `setAltSetting`, `CI::setRngFn`). One contract, one code
  path.
- `Device::begin()` no longer claims to call `tusb_init` internally. It
  initialises the library's own dispatcher and returns; the caller owns
  the platform USB stack lifecycle.
- `Device::task()` drops the commented `tud_task` stubs. The caller's
  main loop calls its own platform's USB task and pumps received UMP
  through `Device::feedRx`.

### Added — platform contract
- `Device::setWriteFn(WriteFn)` — outbound UMP. Library invokes the
  caller's function for every `sendXxx` and the JR heartbeat.
- `Device::feedRx(const uint32_t* words, size_t count)` — inbound UMP.
  Caller pumps RX into the library; chunks transparently to the upstream
  `uint8_t word_count` limit of `midi2_proc_feed`.
- `Device::setNowFn(NowFn)` — monotonic ms clock for the JR heartbeat.
  When unset, the heartbeat never fires (link-safe on bare hosts).
- `Device::setMounted(bool)` / `Device::setAltSetting(uint8_t)` — caller
  informs USB enumeration state.
- `Device::procState()` — public power-user accessor (replaces the
  former private `procStateForTest`).
- `CI::setRngFn(RngFn)` — caller-supplied entropy source. When unset,
  MUID stays at the value seeded in `begin()` (no platform symbol is
  pulled in).

### Removed
- `Device::setTxHookForTest`, `Device::setNowForTest`,
  `Device::procStateForTest`, and the surrounding `MIDI2_CPP_TEST_MODE`
  ifdef block. Replaced by the public hooks above.
- `lib/tinyusb` git submodule and the `.gitmodules` entry. The library
  has zero external dependencies: midi2 C99 stays vendored at
  `src/midi2.{h,c}`, every USB stack is caller-wired, every clock and
  RNG source is caller-wired. `git clone` is the install — no
  `--recurse-submodules`, no fetch from another registry, no
  half-initialised state.

### Compatibility
- C++17 floor enforced via `static_assert(__cplusplus >= 201703L)` in
  `midi2_cpp.h`. Earlier toolchains get a clean diagnostic instead of a
  cryptic template error.

### Pending
- Hardware integration test results.
- v0.1.0 git tag + GitHub Release (suspended pending hardware run +
  per-platform example recipes once the TinyUSB MIDI 2.0 override path
  stabilises).

## [0.1.0] — 2026-06-17 (target)

First public release. Wraps midi2 C99 v0.3.0+ as a C++17 Arduino-style
library for embedded MIDI 2.0 devices.

### `midi2::Device` — UMP transport (M2-104)

- Lifecycle: `begin()` (initialises dispatcher + proc; caller wires
  platform USB lifecycle and clock), `task()` (runs heartbeat),
  `isMounted()`, `altSetting()`.
- All 8 message types covered:
  - **MT 0x0 Utility**: `sendNoop`, `sendJRClock`, `sendJRTimestamp`,
    `sendDctpq`, `sendDeltaClockstamp`. Defensive JR Timestamp heartbeat
    via `enableJRHeartbeat(intervalMs)` (default 500 ms, empirically
    tuned for Linux ALSA polling).
  - **MT 0x1 System** (10 wrappers + generic escape): `sendTuneRequest`,
    `sendClock`, `sendStart`, `sendContinue`, `sendStop`,
    `sendActiveSensing`, `sendSystemReset`, `sendMTC`, `sendSongSelect`,
    `sendSongPosition`, `sendSystemGeneric`.
  - **MT 0x2 MIDI 1.0 CV** (7 wrappers + ByteStreamConverter):
    `sendNoteOn1`, `sendNoteOff1`, `sendCC1`, `sendProgram1`,
    `sendPitchBend1`, `sendChannelPressure1`, `sendPolyPressure1`.
    Inbound MT 0x2 can be auto-upscaled to MT 0x4 callbacks via
    `setUpscaleMt2(true)`.
  - **MT 0x3 SysEx7**: `sendSysEx7(group, data, len)` with automatic
    fragmentation; reassembly via `onSysEx7` callback (512 byte buffer).
  - **MT 0x4 MIDI 2.0 CV** (15 wrappers): NoteOn / NoteOff (with
    attribute type+data), CC, Program (with optional bank), Pitch Bend,
    Channel Pressure, Poly Pressure, RPN, NRPN, Relative RPN/NRPN,
    Per-Note Pitch Bend, Registered Per-Note Controller, Assignable
    Per-Note Controller, Per-Note Management.
  - **MT 0x5 SysEx8**: `sendSysEx8(group, streamId, data, len)` with
    automatic fragmentation; reassembly via `onSysEx8`.
  - **MT 0xD Flex Data** (6 wrappers): `sendTempo`, `sendTimeSignature`,
    `sendMetronome`, `sendKeySignature`,
    `sendChordName(ChordDescriptor)` (20-field struct), `sendFlexText`
    (refuses payloads > 12 bytes; multi-UMP fragmentation deferred).
  - **MT 0xF UMP Stream** (6 wrappers): `sendDeviceIdentity` (mfrId[3]
    MSB-first), `sendEndpointNameUpdate`, `sendProductInstanceIdUpdate`,
    `sendFbNameUpdate`, `sendStartOfClip`, `sendEndOfClip`
    (endpoint-wide, no group field).
- 49 inbound dispatch callbacks via `onXxx(std::function)` setters;
  backed by midi2_dispatch trampolines registered in `begin()`.
- All `sendXxx` return `bool`: `true` = emitted, `false` = dropped on
  back-pressure or refused at API boundary.
- Bit Scaling (M2-115): `scaleUp7to16`, `scaleUp7to32`, `scaleUp14to32`,
  `scaleDown16to7`, `scaleDown32to7`, `scaleDown32to14` static methods,
  exhaustive roundtrip tested (16,640 iterations).
- Field-tested helpers: `setUmpGroup`, `setGroupRemap`,
  `downgradeMt4ToMt2`, `cableEventToUmp`, `setUpscaleMt2`.
- `ByteStreamConverter` inner class: MIDI 1.0 DIN-5 byte stream → UMP
  MT 0x2 / MT 0x3 with running status and SysEx accumulation.

### `midi2::CI` — MIDI-CI v1.2 (M2-101)

- Lifecycle: `begin(mfrId[3], family, model, version, ciCat=0x1C)`
  enables Profile + PE + PI by default.
- MUID: `muid()`, `regenerateMuid()`. Auto-regeneration on collision and
  on Invalidate MUID via the caller-supplied RNG (`CI::setRngFn`).
- Convenience responder (M2-101 Appendix E) auto-replies to Discovery,
  Profile Inquiry, PE Capability, PE Get; opt-out via setting custom
  callbacks. NAK-on-unknown enabled by default.
- Discovery: `sendDiscoveryInquiry()` (Initiator), `onDiscovery` /
  `onDiscoveryReply` notifications, `onInvalidateMuid`.
- Profile (M2-101 §7): `addProfile`, `removeProfile`, callbacks for
  `onProfileInquiry`, `onProfileEnable`, `onProfileDisable`,
  `onProfileAdded`, `onProfileRemoved`, `onProfileDetailsInquiry`,
  `onProfileSpecificData`. Storage tunable via `MIDI2_CPP_MAX_PROFILES`
  (default 8).
- Property Exchange (M2-101 §8, M2-103, M2-105):
  - Registry: `addProperty(name, getter, setter=nullptr)` (read-only by
    default), `addPropertyStatic(name, value)`, `removeProperty`.
    Storage tunable via `MIDI2_CPP_MAX_PROPERTIES` (default 8).
  - Subscribe / Notify state machine:
    `setPropertySubscribable(name, true)`,
    `notifyPropertyChanged(name)` for fan-out, `subscriberCount()`.
    Subscriber registry tunable via `MIDI2_CPP_MAX_SUBSCRIBERS`
    (default 4).
  - PE callbacks deliver raw bytes (header + body) instead of
    NUL-terminated strings, to avoid silent truncation of large JSON
    payloads.
- Process Inquiry (M2-101 §9 + Appendix F):
  `setMidiReport(msgDataControl, systemBitmap, channelBitmap,
  noteBitmap)`, `onPICapability`, `onMidiReportInquiry`.

### Bridge & internals

- `Device` ↔ `CI` bridge via friend-only methods (`_setCiSysExHook`,
  `_ciWriteFnContext`); user-facing `onSysEx7` keeps working alongside
  CI's SysEx routing.
- Two-hop dispatch context: `proc.context = &dispatch`,
  `dispatch.context = DeviceState*`. Lets `midi2_dispatch_feed` and
  reassembled SysEx callbacks coexist without upstream patches.
- `~CI()` clears Device's CI hook to avoid use-after-free if CI dies
  before Device.
- `s->ci.context = s` in `CI::begin()` so PE getter/setter trampolines
  can locate the user lambdas.
- `removeProperty` mirrors midi2_ci's left-shift on the parallel
  `pe_getters[]` / `pe_setters[]` arrays (alignment invariant).

### Build, packaging, tests

- Triple build: Arduino `library.properties`, PlatformIO
  `library.json`, CMake.
- midi2 C99 v0.3.0 vendored stb-style at `src/midi2.{h,c}`; refresh
  script at `scripts/vendor_midi2.sh`.
- 70+ assertions across 5 host-side test binaries (DIY
  `TEST/PASS/FAIL/CHECK` macros), all green under `-Wall -Wextra
  -Wpedantic` on gcc + clang (Ubuntu, macOS).
- GitHub Actions CI: host-tests matrix (Ubuntu / macOS) + strict
  warnings (gcc / clang `-Werror`).
- 8 Arduino sketches in `examples/` covering the v0.1 surface as
  library-API skeletons; per-platform glue lands when the TinyUSB
  MIDI 2.0 override path stabilises.

### Known limitations (deferred to v0.1.x)

- AVR Uno (2 KB RAM) is out of scope (5.4 KB total heap usage).
- `setMaxSysexSize` not exposed (midi2 C99 lacks the setter upstream).
- 5 Initiator-role senders (`sendEndpointInfoInquiry/Reply`, `sendAck`,
  `sendProfileDetailsReply`, `sendProfileSpecificData`) deferred — the
  convenience responder covers the common Receiver flows.
- `sendFlexText` does not yet fragment payloads > 12 bytes; multi-UMP
  format=1/2/3 chain pending.
- MT 0x2 named senders (per-message `sendNoteOn1` etc) cover the simple
  case; bridges UMP → DIN-5 (UMP → byte stream) deferred.

# rp2040-midi2 — example for the [midi2_cpp](../..) library

Full-spec USB MIDI 2.0 device example for the **Raspberry Pi Pico (RP2040)**. Headless, single-file showcase of every MIDI 2.0 message category beyond MIDI 1.0. Lives at `midi2_cpp/examples/rp2040-midi2/` and consumes the parent library directly (no vendoring).

![rp2040-midi2 banner — Raspberry Pi Pico GPIO pinout](board/banner.png)

> ⚠️ **TinyUSB override — not yet upstream.** The USB MIDI 2.0 device + host class drivers this project depends on live in TinyUSB [PR #3571](https://github.com/hathach/tinyusb/pull/3571), still under review. Until that PR merges into `hathach/tinyusb`, this build pulls a personal fork ([`sauloverissimo/tinyusb` branch `feat/midi2-device-host-driver`](https://github.com/sauloverissimo/tinyusb/tree/feat/midi2-device-host-driver)) at a pinned SHA. Treat the repo as **beta**: when the PR lands upstream the override goes away and this README will point at the official TinyUSB.

## What this is

`rp2040-midi2` is the platform layer for a family of MIDI 2.0 devices on the RP2040. It owns:

- Pico SDK board init (`board_init`, `tusb_init`)
- TinyUSB MIDI 2.0 device class wiring (override of TinyUSB **PR #3571 fork**, *not yet merged upstream* — fetched on demand via CMake FetchContent)
- USB descriptors (VID `0xCAFE`, PID `0x4070`)
- The five [midi2_cpp](https://github.com/sauloverissimo/midi2_cpp) platform hooks already wired: `setWriteFn`, `feedRx`, `setNowFn`, `setMounted` / `setAltSetting`, `CI::setRngFn`

After `rp2040_midi2::init(midi, ci)`, the application sees only `m2device` and `m2ci`. It never touches `tud_*`, `pico_*`, or any USB symbol. Replicating the same shape on another board is a matter of writing `<board>_midi2.{h,cpp}` with the same two-function surface.

## What this is not

Not a finished product. The bundled `rp2040-midi2-showcase` executable is a **demo application** built on top of this core: it exercises every category of UMP message MIDI 2.0 brings beyond MIDI 1.0, then loops. Real applications copy this core and replace the showcase with their own behaviour layer:

- `rp2040-midi2-player` — adds an SMF parser + playback engine
- `rp2040-midi2-bridge` — adds routing + filtering + MIDI 1.0 → 2.0 upscaling
- *(your project here)*

## Identification

| Field | Value |
|---|---|
| USB VID | `0xCAFE` |
| USB PID | `0x4070` |
| USB Manufacturer | `github.com/sauloverissimo` |
| USB Product | `rp2040-midi2` |
| MIDI-CI Manufacturer ID | `{0x7D, 0x00, 0x00}` (MIDI Association educational/non-commercial prefix) |
| MIDI-CI Family / Model / Version | `0x0001 / 0x0001 / 0x00010000` |

## Build

Requirements:

- **Pico SDK 2.x** with `PICO_SDK_PATH` exported
- **arm-none-eabi-gcc** toolchain (Arm GNU embedded, 9+ recommended)
- **CMake 3.14+**
- Internet on the first `cmake -B build` (FetchContent pulls the TinyUSB fork)

```bash
git clone https://github.com/sauloverissimo/midi2_cpp.git
cd midi2_cpp/examples/rp2040-midi2
cmake -B build         # first run fetches the TinyUSB PR #3571 fork (~5 MB, internet required)
cmake --build build -j # offline from here on
```

Flash the resulting `build/rp2040-midi2-showcase.uf2` onto a Pico in BOOTSEL mode (drag-and-drop or `picotool load`).

The TinyUSB fork lives in `build/_deps/tinyusb_fork-src` (gitignored). To point at a working copy of the fork already on disk:

```bash
cmake -B build -DPICO_TINYUSB_PATH=/path/to/your/tinyusb
```

## Hardware

| Pin | Use |
|---|---|
| USB | MIDI 2.0 device (the only USB function — no CDC stdio) |
| GP0 | UART TX (debug print @ 115200 8N1) |
| GP1 | UART RX |
| GP25 | On-board LED (lit while USB is mounted) — original Pico only; on Pico W the LED lives behind the CYW43 chip and needs `pico_cyw43_arch_lwip_threadsafe_background` instead |

## Showcase

What the bundled `rp2040-midi2-showcase` executable demonstrates after enumeration. Constants in [`src/main.cpp`](src/main.cpp) — adjust to taste. Each cycle is ~22 s and loops continuously while the device stays mounted.

**Always-on (boot to forever):**

- **JR Timestamp heartbeat** every 500 ms (MT 0x0 status 0x2) — keeps Linux ALSA polling alive on idle endpoints
- **UMP Stream Discovery responder** (MT 0xF) — replies to host-side Endpoint Discovery and Function Block Discovery with Endpoint Info, Device Identity, Endpoint Name, Product Instance ID, Stream Config Notify, FB Info, FB Name
- **MIDI-CI Discovery + PE Capability + PE Get** auto-replied via `m2ci`'s Appendix E convenience responder
- **1 Custom Profile** registered (id `7D 00 00 01 00`) with Enable/Disable callbacks
- **3 Properties** in PE: static `DeviceInfo`, dynamic `ChannelList`, subscribable `OverlayRate`
- **Process Inquiry** (`setMidiReport`) configured with system + channel + note bitmaps

**Per cycle (~22 s):**

| Scene | Content | Why MIDI 2.0 only |
|---|---|---|
| **A — Flex Data suite** | Tempo (120 BPM), Time Sig (4/4), Key Sig (C major), Metronome, Chord Name (Cmaj7), Start of Clip | MT 0xD + 0xF — no MIDI 1.0 equivalent |
| **B — Per-Note expression stack** | Sustained C4 with Per-Note Pitch Bend (5 Hz vibrato), Registered Per-Note Controller #7 (volume), Assignable Per-Note Controller #74 (brightness), Per-Note Management Reset | Per-Note family does not exist in MIDI 1.0 |
| **C — Resolution showcase** | Chromatic walk C5→G#5 with **16-bit velocity** ramp + **32-bit CC #74** sweep + **32-bit Pitch Bend** ramp + **32-bit Poly Pressure** + **32-bit Channel Pressure** | MIDI 1.0 caps at 7-bit / 14-bit |
| **D — Program + Bank** | Program Change with bank MSB/LSB in a single UMP | MIDI 1.0 needs three messages |
| **E — RPN / NRPN / Relative** | RPN 0/0 (Pitch Bend Sensitivity), NRPN, Relative RPN (+delta), Relative NRPN (-delta) | RPN/NRPN as first-class + Relative are MIDI 2.0 only |
| **F — Note Attribute** | Note On with `attribute_type=0x03` (pitch_7_9), E4 +50 cents | Microtonal Note Attribute is MIDI 2.0 only |
| **G — SysEx8** | 16 raw 8-bit bytes with no 7-bit aliasing | MT 0x5 is MIDI 2.0 only |
| **H — Delta Clockstamp** | DCTPQ=480 + Delta Clockstamp=240 ticks | MT 0x0 utility messages are MIDI 2.0 only |
| **I — PE Notify** | Broadcast `OverlayRate` change to subscribers (value increments per cycle) | Property Exchange is MIDI 2.0 only |
| **J — End of Clip** | Sequencer End of Clip marker | MT 0xF status 0x21, MIDI 2.0 only |

Every scene logs to UART so a USB-Serial adapter on GP0 lets you watch the timeline live. Captured screenshots of the device under Windows MIDI Services Console live in [`endpoint_monitor/`](endpoint_monitor/).

## What lives where

```
midi2_cpp/
├── src/                            parent library (consumed by this example
│                                   via ../../src in this CMakeLists)
└── examples/rp2040-midi2/
    ├── CMakeLists.txt              FetchContent for TinyUSB PR #3571 + targets
    ├── pico_sdk_import.cmake
    ├── README.md
    ├── board/
    │   ├── banner.png              repo banner (used in this README)
    │   └── rp2040pinout.png        Pico GPIO reference
    ├── endpoint_monitor/           Windows MIDI Services screenshots
    └── src/
        ├── rp2040_midi2.h          public API of the core (init + task)
        ├── rp2040_midi2.cpp        Pico SDK + tinyusb glue, all hooks wired
        ├── usb_descriptors.c       USB MIDI 2.0 descriptors
        ├── tusb_config.h           TinyUSB config (1 group, 1 function block)
        └── main.cpp                showcase entry — full-spec MIDI 2.0 demo
```

The TinyUSB PR #3571 fork is fetched at configure time into `build/_deps/tinyusb_fork-src` (gitignored). This example folder itself is ~1 MB.

## License

MIT — inherits the parent [`midi2_cpp` LICENSE](../../LICENSE). The TinyUSB fork (fetched on demand) is MIT (upstream by hathach, fork by sauloverissimo carrying the MIDI 2.0 class drivers from the still-open [PR #3571](https://github.com/hathach/tinyusb/pull/3571)).

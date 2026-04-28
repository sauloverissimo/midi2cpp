# [midi2_cpp](../..) | Device MIDI 2.0
## Waveshare RP2350-USB-A

USB MIDI 2.0 **device** example for the **Waveshare RP2350-USB-A**. Headless, single-file showcase of every MIDI 2.0 message category beyond MIDI 1.0, identical in behaviour to the [`rp2040-midi2`](../rp2040-midi2) example with the board target swapped to the Pico SDK generic `pico2` (the SDK in this checkout has no dedicated header for the Waveshare RP2350-USB-A) and the identity strings rebranded to `waveshare-RP2350-USB-A`. Lives at `midi2_cpp/examples/waveshare-rp2350-usb-a-midi2/` and consumes the parent library directly (no vendoring).

This recipe drives only the USB-C side as a MIDI 2.0 device. The board's USB-A host port is exercised by the sibling [`waveshare-rp2350-usb-a-bridge-midi2`](../waveshare-rp2350-usb-a-bridge-midi2) recipe; that path requires a hardware modification (R13 desolder) which this device-only recipe does not.

![waveshare-RP2350-USB-A board photo](board/banner.png)

> ⚠️ **TinyUSB override, not yet upstream.** The USB MIDI 2.0 device class driver this project depends on lives in TinyUSB [PR #3571](https://github.com/hathach/tinyusb/pull/3571), still under review. Until that PR merges into `hathach/tinyusb`, this build pulls a personal fork ([`sauloverissimo/tinyusb` branch `feat/midi2-device-host-driver`](https://github.com/sauloverissimo/tinyusb/tree/feat/midi2-device-host-driver)) at a pinned SHA. Treat the build as **beta**: when the PR lands upstream the override goes away and this README will point at the official TinyUSB.

## What this is

`waveshare-rp2350-usb-a-midi2` is the platform layer for a MIDI 2.0 device on the Waveshare RP2350-USB-A. It owns:

- Pico SDK board init (`board_init`, `tusb_init`) on `PICO_BOARD=pico2` (generic RP2350 target)
- TinyUSB MIDI 2.0 device class wiring (override of TinyUSB **PR #3571 fork**, *not yet merged upstream*, fetched on demand via CMake FetchContent)
- USB descriptors (VID `0xCAFE`, PID `0x4076`, product string `waveshare-RP2350-USB-A`)
- The five [midi2_cpp](https://github.com/sauloverissimo/midi2_cpp) platform hooks already wired: `setWriteFn`, `feedRx`, `setNowFn`, `setMounted` / `setAltSetting`, `CI::setRngFn`

After `rp2040_midi2::init(midi, ci)` (the same shared core as the `rp2040-midi2` example, no rename of the namespace because the underlying RP-family USB IP is the same), the application sees only `m2device` and `m2ci`. It never touches `tud_*`, `pico_*`, or any USB symbol.

## What this is not

Not a finished product. The bundled `waveshare-rp2350-usb-a-midi2-showcase` executable is a **demo application** built on top of the shared core: it exercises every category of UMP message MIDI 2.0 brings beyond MIDI 1.0, then loops. Real applications copy this core and replace the showcase with their own behaviour layer.

## Identification

| Field | Value |
|---|---|
| USB VID | `0xCAFE` |
| USB PID | `0x4076` |
| USB Manufacturer | `github.com/sauloverissimo` |
| USB Product | `waveshare-RP2350-USB-A` |
| MIDI-CI Manufacturer ID | `{0x7D, 0x00, 0x00}` (MIDI Association educational/non-commercial prefix) |
| MIDI-CI Family / Model / Version | `0x0001 / 0x0001 / 0x00010000` |
| UMP Endpoint Name | `waveshare-RP2350-USB-A` |
| Product Instance ID | `waveshare-RP2350-USB-A-showcase-0001` |

The PID `0x4076` distinguishes this example from the other midi2_cpp device recipes (`rp2040-midi2` `0x4070`, `waveshare-rp2040-midi2` `0x4072`) so a host enumerating multiple boards on the same machine sees distinct endpoints.

## Build

Requirements:

- **Pico SDK 2.x** with `PICO_SDK_PATH` exported. RP2350 support is in 2.0+.
- **arm-none-eabi-gcc** toolchain (Arm GNU embedded, 9+ recommended). The Pico SDK selects the Cortex-M33 variant automatically when `PICO_BOARD=pico2`; no toolchain swap needed.
- **CMake 3.14+**
- Internet on the first `cmake -B build` (FetchContent pulls the TinyUSB fork)

```bash
git clone https://github.com/sauloverissimo/midi2_cpp.git
cd midi2_cpp/examples/waveshare-rp2350-usb-a-midi2
cmake -B build         # first run fetches the TinyUSB PR #3571 fork (~5 MB, internet required)
cmake --build build -j # offline from here on
```

Flash the resulting `build/waveshare-rp2350-usb-a-midi2-showcase.uf2` onto a Waveshare RP2350-USB-A in BOOTSEL mode (hold BOOT, plug USB-C, drop the UF2 onto the RP2350 mass storage that appears).

The TinyUSB fork lives in `build/_deps/tinyusb_fork-src` (gitignored). To point at a working copy of the fork already on disk:

```bash
cmake -B build -DPICO_TINYUSB_PATH=/path/to/your/tinyusb
```

## Hardware

| Pin | Use |
|---|---|
| USB-C | MIDI 2.0 device (the only USB function in this recipe, no CDC stdio) |
| USB-A | unused in this recipe; driven by the bridge recipe |
| GP0  | UART TX (debug print @ 115200 8N1) |
| GP1  | UART RX |
| BOOT | Hold while plugging USB-C to enter BOOTSEL mode |
| RST  | On-board reset button |

The Waveshare RP2350-USB-A is a 21 x 51 mm RP2350 board with two USB ports: USB-C (used here as the device endpoint to the host PC) and USB-A (a full-size Type-A receptacle wired to GP12 / GP13 through PIO-USB, used by the sibling bridge recipe). The board has BOOT and RST tactile buttons, header pads on both edges (castellated and through-hole), and an on-board RGB LED. None of the LED, the USB-A port, the PIO-USB pins, or the headers are exercised by this device-only recipe.

## Showcase

What the bundled `waveshare-rp2350-usb-a-midi2-showcase` executable demonstrates after enumeration. Constants in [`src/main.cpp`](src/main.cpp), adjust to taste. Each cycle is ~22 s and loops continuously while the device stays mounted.

**Always-on (boot to forever):**

- **JR Timestamp heartbeat** every 500 ms (MT 0x0 status 0x2), keeps Linux ALSA polling alive on idle endpoints
- **UMP Stream Discovery responder** (MT 0xF), replies to host-side Endpoint Discovery and Function Block Discovery with Endpoint Info, Device Identity, Endpoint Name, Product Instance ID, Stream Config Notify, FB Info, FB Name
- **MIDI-CI Discovery + PE Capability + PE Get** auto-replied via `m2ci`'s Appendix E convenience responder
- **1 Custom Profile** registered (id `7D 00 00 01 00`) with Enable/Disable callbacks
- **3 Properties** in PE: static `DeviceInfo`, dynamic `ChannelList`, subscribable `OverlayRate`
- **Process Inquiry** (`setMidiReport`) configured with system + channel + note bitmaps

**Per cycle (~22 s):**

| Scene | Content | Why MIDI 2.0 only |
|---|---|---|
| **A, Flex Data suite** | Tempo (120 BPM), Time Sig (4/4), Key Sig (C major), Metronome, Chord Name (Cmaj7), Start of Clip | MT 0xD + 0xF, no MIDI 1.0 equivalent |
| **B, Per-Note expression stack** | Sustained C4 with Per-Note Pitch Bend (5 Hz vibrato), Registered Per-Note Controller #7 (volume), Assignable Per-Note Controller #74 (brightness), Per-Note Management Reset | Per-Note family does not exist in MIDI 1.0 |
| **C, Resolution showcase** | Chromatic walk C5 to G#5 with **16-bit velocity** ramp + **32-bit CC #74** sweep + **32-bit Pitch Bend** ramp + **32-bit Poly Pressure** + **32-bit Channel Pressure** | MIDI 1.0 caps at 7-bit / 14-bit |
| **D, Program + Bank** | Program Change with bank MSB/LSB in a single UMP | MIDI 1.0 needs three messages |
| **E, RPN / NRPN / Relative** | RPN 0/0 (Pitch Bend Sensitivity), NRPN, Relative RPN (+delta), Relative NRPN (-delta) | RPN/NRPN as first-class + Relative are MIDI 2.0 only |
| **F, Note Attribute** | Note On with `attribute_type=0x03` (pitch_7_9), E4 +50 cents | Microtonal Note Attribute is MIDI 2.0 only |
| **G, SysEx8** | 16 raw 8-bit bytes with no 7-bit aliasing | MT 0x5 is MIDI 2.0 only |
| **H, Delta Clockstamp** | DCTPQ=480 + Delta Clockstamp=240 ticks | MT 0x0 utility messages are MIDI 2.0 only |
| **I, PE Notify** | Broadcast `OverlayRate` change to subscribers (value increments per cycle) | Property Exchange is MIDI 2.0 only |
| **J, End of Clip** | Sequencer End of Clip marker | MT 0xF status 0x21, MIDI 2.0 only |

Every scene logs to UART so a USB-Serial adapter on GP0 lets you watch the timeline live.

## Validation

Pair this device with one of the host examples to validate the round trip end to end at the wire level:

- [`adafruit-feather-rp2040-host-midi2`](../adafruit-feather-rp2040-host-midi2) shows decoded UMP on a 128x64 SSD1306 OLED.
- [`adafruit-feather-rp2040-bridge-midi2`](../adafruit-feather-rp2040-bridge-midi2) forwards to a PC running Microsoft MIDI Services Console.
- [`waveshare-rp2350-usb-a-bridge-midi2`](../waveshare-rp2350-usb-a-bridge-midi2) is the host/bridge variant of this same board; flash it on a second RP2350-USB-A and connect the two boards via their USB-A and USB-C ports for a same-family round trip.

For a direct PC validation, plug the device straight into a laptop and inspect the enumeration with [Microsoft MIDI Services Console](https://github.com/microsoft/MIDI) on Windows, `amidi -l` on Linux, or Audio MIDI Setup on macOS. Expected report on the Microsoft MIDI Services Console:

- Native data format: Universal MIDI Packet
- Protocol: Midi2
- MIDI 2.0 Protocol: True
- Name: `waveshare-RP2350-USB-A`
- USB VID / PID: `CAFE / 4076`

### Bench setup

![bench top-down with the Waveshare RP2350-USB-A wired up](monitor/stack.png)
![laptop running Microsoft MIDI Services Console next to the bench](monitor/bridge.png)
![Microsoft MIDI Services Console message log](monitor/windows.png)

## What lives where

```
midi2_cpp/
├── src/                            parent library (consumed by this example
│                                   via ../../src in this CMakeLists)
└── examples/waveshare-rp2350-usb-a-midi2/
    ├── CMakeLists.txt              FetchContent for TinyUSB PR #3571 + targets
    ├── pico_sdk_import.cmake
    ├── README.md
    ├── board/
    │   ├── banner.png                       repo banner (used at the top of this README)
    │   ├── board.png                        Waveshare RP2350-USB-A product photo (sourced from Waveshare wiki)
    │   ├── pinout.png                       Pro Micro pinout diagram for the RP2350-USB-A
    │   └── RP2350-USB-A-Schematic.pdf       Waveshare RP2350-USB-A schematic
    ├── monitor/
    │   ├── bridge.png                       laptop running Microsoft MIDI Services Console next to the bench
    │   ├── stack.png                        bench top-down with the board on a protoboard
    │   └── windows.png                      Microsoft MIDI Services Console message log capture
    └── src/
        ├── rp2040_midi2.h          public API of the core (init + task), shared name with rp2040-midi2 (same RP-family USB IP)
        ├── rp2040_midi2.cpp        Pico SDK + TinyUSB glue, all hooks wired
        ├── usb_descriptors.c       USB MIDI 2.0 descriptors (VID 0xCAFE, PID 0x4076, product `waveshare-RP2350-USB-A`)
        ├── tusb_config.h           TinyUSB config (1 group, 1 function block)
        └── main.cpp                showcase entry, full-spec MIDI 2.0 demo
```

The TinyUSB PR #3571 fork is fetched at configure time into `build/_deps/tinyusb_fork-src` (gitignored). This example folder is under 4 MB on disk; the heaviest items are the bench photographs under `monitor/`.

## License

MIT, inherits the parent [`midi2_cpp` LICENSE](../../LICENSE). The TinyUSB fork (fetched on demand) is MIT (upstream by hathach, fork by sauloverissimo carrying the MIDI 2.0 class drivers from the still-open [PR #3571](https://github.com/hathach/tinyusb/pull/3571)). The Waveshare RP2350-USB-A hardware reference assets bundled under `board/` (board photo, pinout, schematic) are © Waveshare Electronics, redistributed for documentation purposes.

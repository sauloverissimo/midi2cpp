# [midi2_cpp](../..) | Device MIDI 2.0
## Waveshare RP2040 Pi Zero

USB MIDI 2.0 **device** example for the **Waveshare RP2040 Pi Zero**. Headless, single-file showcase of every MIDI 2.0 message category beyond MIDI 1.0, identical in behaviour to the [`rp2040-midi2`](../rp2040-midi2) example with the board target swapped to `waveshare_rp2040_zero` and the identity strings rebranded to `RP2040PiZero`. Lives at `midi2_cpp/examples/waveshare-rp2040-midi2/` and consumes the parent library directly (no vendoring).

![waveshare-rp2040-midi2 banner, RP2040 Pi Zero pinout](board/banner.png)

> ⚠️ **TinyUSB override, not yet upstream.** The USB MIDI 2.0 device class driver this project depends on lives in TinyUSB [PR #3571](https://github.com/hathach/tinyusb/pull/3571), still under review. Until that PR merges into `hathach/tinyusb`, this build pulls a personal fork ([`sauloverissimo/tinyusb` branch `feat/midi2-device-host-driver`](https://github.com/sauloverissimo/tinyusb/tree/feat/midi2-device-host-driver)) at a pinned SHA. Treat the build as **beta**: when the PR lands upstream the override goes away and this README will point at the official TinyUSB.

## What this is

`waveshare-rp2040-midi2` is the platform layer for a MIDI 2.0 device on the Waveshare RP2040 Zero. It owns:

- Pico SDK board init (`board_init`, `tusb_init`) on `PICO_BOARD=waveshare_rp2040_zero`
- TinyUSB MIDI 2.0 device class wiring (override of TinyUSB **PR #3571 fork**, *not yet merged upstream*, fetched on demand via CMake FetchContent)
- USB descriptors (VID `0xCAFE`, PID `0x4072`, product string `RP2040PiZero`)
- The five [midi2_cpp](https://github.com/sauloverissimo/midi2_cpp) platform hooks already wired: `setWriteFn`, `feedRx`, `setNowFn`, `setMounted` / `setAltSetting`, `CI::setRngFn`

After `rp2040_midi2::init(midi, ci)` (the same shared core as the `rp2040-midi2` example, no rename of the namespace because the underlying USB IP is the same), the application sees only `m2device` and `m2ci`. It never touches `tud_*`, `pico_*`, or any USB symbol. Replicating the same shape on yet another RP2040 board is a matter of swapping `PICO_BOARD` and the identity strings, exactly what this example does relative to `rp2040-midi2`.

## What this is not

Not a finished product. The bundled `waveshare-rp2040-midi2-showcase` executable is a **demo application** built on top of the shared core: it exercises every category of UMP message MIDI 2.0 brings beyond MIDI 1.0, then loops. Real applications copy this core and replace the showcase with their own behaviour layer.

## Identification

| Field | Value |
|---|---|
| USB VID | `0xCAFE` |
| USB PID | `0x4072` |
| USB Manufacturer | `github.com/sauloverissimo` |
| USB Product | `RP2040PiZero` |
| MIDI-CI Manufacturer ID | `{0x7D, 0x00, 0x00}` (MIDI Association educational/non-commercial prefix) |
| MIDI-CI Family / Model / Version | `0x0001 / 0x0001 / 0x00010000` |
| UMP Endpoint Name | `RP2040PiZero` |
| Product Instance ID | `RP2040PiZero-showcase-0001` |

The PID `0x4072` distinguishes this example from `rp2040-midi2` (`0x4070`) and the bridge (`0x4071`) so a host enumerating multiple devices on the same machine sees three distinct endpoints.

## Build

Requirements:

- **Pico SDK 2.x** with `PICO_SDK_PATH` exported
- **arm-none-eabi-gcc** toolchain (Arm GNU embedded, 9+ recommended)
- **CMake 3.14+**
- Internet on the first `cmake -B build` (FetchContent pulls the TinyUSB fork)

```bash
git clone https://github.com/sauloverissimo/midi2_cpp.git
cd midi2_cpp/examples/waveshare-rp2040-midi2
cmake -B build         # first run fetches the TinyUSB PR #3571 fork (~5 MB, internet required)
cmake --build build -j # offline from here on
```

Flash the resulting `build/waveshare-rp2040-midi2-showcase.uf2` onto a Waveshare RP2040 Zero in BOOTSEL mode (hold BOOT, plug USB-C, drop the UF2 onto the RPI-RP2 mass storage that appears).

The TinyUSB fork lives in `build/_deps/tinyusb_fork-src` (gitignored). To point at a working copy of the fork already on disk:

```bash
cmake -B build -DPICO_TINYUSB_PATH=/path/to/your/tinyusb
```

## Hardware

| Pin | Use |
|---|---|
| USB-C | MIDI 2.0 device (the only USB function, no CDC stdio) |
| GP0  | UART TX (debug print @ 115200 8N1) |
| GP1  | UART RX |
| GP16 | On-board WS2812 RGB LED (not driven by this example) |
| BOOT | Hold while plugging USB-C to enter BOOTSEL mode |

The Waveshare RP2040 Zero is a compact (18.0 x 23.5 mm) RP2040 board with USB-C on the long edge and a single WS2812 RGB LED on GP16. There is no on-board reset button on the standard model; power-cycle to reset.

## Showcase

What the bundled `waveshare-rp2040-midi2-showcase` executable demonstrates after enumeration. Constants in [`src/main.cpp`](src/main.cpp), adjust to taste. Each cycle is ~22 s and loops continuously while the device stays mounted.

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

Pair this device with one of our host examples to validate the round trip end to end at the wire level:

- [`adafruit-feather-rp2040-host-midi2`](../adafruit-feather-rp2040-host-midi2) shows decoded UMP on a 128x64 SSD1306 OLED.
- [`adafruit-feather-rp2040-bridge-midi2`](../adafruit-feather-rp2040-bridge-midi2) forwards to a PC running Microsoft MIDI Services Console.

Both consume the same TinyUSB PR #3571 fork plus the Pico-PIO-USB SHA pinned in their respective `CMakeLists.txt`. Plug a USB-C-male to USB-A-male cable from the Waveshare into the Feather's USB-A port and the showcase cycle becomes visible on the host side.

The bench setup below shows the Waveshare RP2040 Pi Zero plugged straight into a laptop running [Microsoft MIDI Services Console](https://github.com/microsoft/MIDI). The console reports `Native data format: Universal MIDI Packet`, `Protocol: Midi2`, `MIDI 2.0 Protocol: True`, the declared `RP2040PiZero` identity, and the `0xCAFE:0x4072` VID:PID, confirming end-to-end MIDI 2.0 enumeration:

![bench setup with Waveshare RP2040 Pi Zero plugged into a laptop](monitor/stack.png)
![Microsoft MIDI Services Console showing RP2040PiZero identity](monitor/windows.png)

## What lives where

```
midi2_cpp/
├── src/                            parent library (consumed by this example
│                                   via ../../src in this CMakeLists)
└── examples/waveshare-rp2040-midi2/
    ├── CMakeLists.txt              FetchContent for TinyUSB PR #3571 + targets
    ├── pico_sdk_import.cmake
    ├── README.md
    ├── board/
    │   ├── banner.png                       repo banner (used in this README)
    │   ├── pinout_front.png                 RP2040 Pi Zero front-side GPIO reference
    │   ├── pinout_back.png                  RP2040 Pi Zero back-side reference
    │   └── RP2040-PiZero-Schematic.pdf      Waveshare RP2040 Pi Zero schematic
    ├── monitor/
    │   ├── stack.png                        bench setup: device plugged into a laptop
    │   └── windows.png                      Microsoft MIDI Services Console identity view
    └── src/
        ├── rp2040_midi2.h          public API of the core (init + task), shared name with rp2040-midi2 (same RP2040 USB IP)
        ├── rp2040_midi2.cpp        Pico SDK + TinyUSB glue, all hooks wired
        ├── usb_descriptors.c       USB MIDI 2.0 descriptors (VID 0xCAFE, PID 0x4072, product `RP2040PiZero`)
        ├── tusb_config.h           TinyUSB config (1 group, 1 function block)
        └── main.cpp                showcase entry, full-spec MIDI 2.0 demo
```

The TinyUSB PR #3571 fork is fetched at configure time into `build/_deps/tinyusb_fork-src` (gitignored). This example folder itself is ~1 MB.

## License

MIT, inherits the parent [`midi2_cpp` LICENSE](../../LICENSE). The TinyUSB fork (fetched on demand) is MIT (upstream by hathach, fork by sauloverissimo carrying the MIDI 2.0 class drivers from the still-open [PR #3571](https://github.com/hathach/tinyusb/pull/3571)).

# [midi2_cpp](../..) | Bridge MIDI 2.0
## Waveshare RP2350-USB-A

Transparent USB MIDI 2.0 **bridge** on the **Waveshare RP2350-USB-A**. Runs TinyUSB host on USB-A (PIO-USB GP12/GP13) and TinyUSB device on USB-C (native USB) in the same firmware, forwarding UMP between them so any MIDI 2.0 device plugged into USB-A appears on the PC as a 16-group MIDI 2.0 endpoint named `waveshare-RP2350-USB-A bridge`. Lives at `midi2_cpp/examples/waveshare-rp2350-usb-a-bridge-midi2/` and consumes the parent library directly (no vendoring).

![waveshare-RP2350-USB-A board photo](board/banner.png)

> ⚠️ **TinyUSB override, not yet upstream.** The USB MIDI 2.0 device + host class drivers this project depends on live in TinyUSB [PR #3571](https://github.com/hathach/tinyusb/pull/3571), still under review. Until that PR merges into `hathach/tinyusb`, this build pulls a personal fork ([`sauloverissimo/tinyusb` branch `feat/midi2-device-host-driver`](https://github.com/sauloverissimo/tinyusb/tree/feat/midi2-device-host-driver)) at a pinned SHA. Treat the build as **beta**: when the PR lands upstream the override goes away and this README will point at the official TinyUSB.

> 🔧 **Hardware modification required.** The Waveshare RP2350-USB-A ships with a 1.5 kΩ pull-up resistor (`R13`) on the USB-A `D+` line. That pull-up biases the line for **device** mode; in **host** mode it prevents the RP2350 from detecting low-speed devices and hot-plug events. **`R13` must be desoldered before the bridge can enumerate anything on the USB-A port.** With this modification, the USB-A connector on this board can no longer be used as a device; only as a host. Photos and a step-by-step removal procedure are documented at [Quentin Santos' write-up](https://qsantos.fr/2025/11/21/fixing-the-rp2350-usb-a-not-working-as-usb-host/).

## What this is

`waveshare-rp2350-usb-a-bridge-midi2` is the platform layer for a dual-stack USB MIDI 2.0 bridge on the Waveshare RP2350-USB-A. It owns:

- Pico SDK board init (`board_init`) on `PICO_BOARD=pico2` (generic RP2350 target)
- TinyUSB **device** stack on rhport 0 (native USB-C, DAW-facing)
- TinyUSB **host** stack on rhport 1 (PIO-USB GP12/GP13, upstream-facing)
- Single-threaded `ump_router` ring buffer that forwards UMP between the two stacks one message per main-loop iteration
- USB-MIDI 1.0 uplift on the host side: upstream `alt=0` cable events (CIN 0x8..0xE) become UMP MT 0x2 so the PC always sees clean MIDI 2.0
- Hot-swap watchdog: `tuh_deinit + tusb_init` after the upstream device has been gone for `MIDI2_CPP_BRIDGE_WATCHDOG_MS`
- Optional 128x64 SSD1306 OLED on I2C1 (GP2/GP3) showing live forwarded UMP with arrow markers

After `feather_bridge::init()`, the application sees only the bridge surface (`task`, `upstream_present`, `downstream_present`, `send_to_pc`). It never touches `tud_*`, `tuh_*`, `pico_*`, or any USB symbol. The internal namespace is named `feather_bridge` for cross-recipe consistency with `adafruit-feather-rp2040-bridge-midi2`; the wiring is the same dual-stack pattern, only the PIO-USB pin defaults change.

## What this is not

Not a finished product. The bundled `waveshare-rp2350-usb-a-bridge-midi2-showcase` executable is a **demo application** that renders forwarded UMP on a 128x64 SSD1306 OLED with arrow markers (`>` upstream→PC, `<` PC→upstream) and emits a standalone showcase pattern when no upstream device is plugged in. The OLED is optional; without it, the bridge still runs and UART debug logs cover the same events. Real applications copy this core and replace the showcase with their own behaviour layer.

## Topology

```
                                 ┌──────────────────────────────────┐
PC / DAW ───── USB-C ───────────►│ Waveshare RP2350-USB-A           │
                                 │   rhport 0 (native USB device)   │
                                 │      ▲                           │
                                 │      │ ump_router (1 msg/iter)   │
                                 │      ▼                           │
                                 │   rhport 1 (PIO-USB host, GP12/13)│
                                 └──────────────────────────────────┘
                                          ▲
                                          │ USB-A (R13 desoldered)
                                          │
                                  MIDI 2.0 device
                                  (or MIDI 1.0, uplifted)
```

## Identification

What the PC sees on the device side (USB-C):

| Field | Value |
|---|---|
| USB VID | `0xCAFE` |
| USB PID | `0x4077` |
| USB Manufacturer | `github.com/sauloverissimo` |
| USB Product | `waveshare-RP2350-USB-A bridge` |
| MIDI 2.0 Groups | 16 (1:1 passthrough, group N upstream becomes group N to PC) |
| Function Blocks | 1 (covers all groups) |
| UMP Endpoint Name | `waveshare-RP2350-USB-A bridge` |

## Build

Requirements:

- **Pico SDK 2.x** with `PICO_SDK_PATH` exported. RP2350 support is in 2.0+.
- **arm-none-eabi-gcc** toolchain (Arm GNU embedded, 9+ recommended)
- **CMake 3.14+**
- Internet on the first `cmake -B build` (FetchContent pulls TinyUSB fork + Pico-PIO-USB)

```bash
git clone https://github.com/sauloverissimo/midi2_cpp.git
cd midi2_cpp/examples/waveshare-rp2350-usb-a-bridge-midi2
cmake -B build         # first run fetches deps (~5 MB TinyUSB + ~1 MB Pico-PIO-USB)
cmake --build build -j # offline from here on
```

Flash the resulting `build/waveshare-rp2350-usb-a-bridge-midi2-showcase.uf2` onto the board in BOOTSEL mode (drag-and-drop or `picotool load`).

To use a local fork or working copy on disk:

```bash
cmake -B build \
  -DPICO_TINYUSB_PATH=/path/to/your/tinyusb \
  -DPICO_PIO_USB_PATH=/path/to/your/Pico-PIO-USB
```

## Hardware

| Pin | Use |
|---|---|
| GP12 | USB-A D+ (PIO-USB host; series resistor R12 = 27 Ω) |
| GP13 | USB-A D- (PIO-USB host; series resistor R11 = 27 Ω) |
| GP2  | I2C1 SDA (optional STEMMA QT / breadboard SSD1306 0x3C) |
| GP3  | I2C1 SCL (optional STEMMA QT / breadboard SSD1306 0x3C) |
| GP0  | UART TX (debug print @ 115200 8N1) |
| GP1  | UART RX |
| USB-C | programming + bridged MIDI 2.0 endpoint to the PC (CDC stdio disabled, UART only) |

| Component | Use |
|---|---|
| Waveshare RP2350-USB-A | RP2350 + native USB-C (device) + USB-A via PIO-USB GP12/GP13 (host) |
| Upstream USB MIDI device | source under test, UMP or USB-MIDI 1.0, both supported |
| 128x64 SSD1306 OLED (I2C 0x3C) | optional, live forwarded UMP display |

The board does not have a software-controlled USB-A 5V power gate; VBUS comes through the USB-C connector and a poly fuse, with no firmware step required.

## Showcase

What the bundled `waveshare-rp2350-usb-a-bridge-midi2-showcase` executable demonstrates after enumeration. The showcase runs in three modes, switching automatically based on connectivity:

**Mode `Waiting`**, no PC mount yet:

- Splash + spinner while waiting for USB-C enumeration

**Mode `Showcase`**, PC mounted, no upstream device on USB-A:

- Bridge emits its own UMP from the device side so a connected DAW can validate the link without an upstream
- Chromatic walk C4 to B4: NoteOn/Off every 250 ms (24 steps total, MT 0x4, group 0, ch 0, vel `0xC000`)
- CC #74 (Brightness) 32-bit sweep every 6 s (5 points spread across the 32-bit range)

**Mode `Bridging`**, PC mounted, upstream device on USB-A:

- Showcase pauses, forward path takes over
- Upstream UMP flows raw to the PC (group preserved, no remap)
- PC UMP flows to the upstream (only when upstream is MIDI 2.0 alt=1 in v0.1)
- USB-MIDI 1.0 upstream cable events are uplifted to UMP MT 0x2 so the PC always sees clean MIDI 2.0
- OLED (when present) shows live decoded UMP with `>` markers (upstream→PC) and `<` markers (PC→upstream)

UART debug on GP0 mirrors mount events for headless monitoring.

## v0.1 scope and limitations

- **R13 desolder is mandatory.** Without it, the host side never enumerates. With it, the USB-A port stops working as a device on this board.
- **Single upstream device** at a time (idx 0). A second device plugged in is enumerated by TinyUSB but not forwarded; no traffic flows.
- **MIDI 1.0 uplift is one-way**: upstream cable events become UMP MT 0x2 on the PC (`>` direction). PC to upstream UMP is forwarded only when the upstream is MIDI 2.0; when it is MIDI 1.0 alt=0, downstream UMP is dropped silently. v0.2 will add UMP to cable conversion for full bidirectional MIDI 1.0 support.
- **Group remap is 1:1**: whatever group the upstream emits is the group the PC sees.
- **No CI bridging**: each USB link runs its own MIDI-CI Initiator/Responder when applicable.
- **No SSD1306 onboard**: the OLED is optional, wire one to GP2 (SDA) / GP3 (SCL) on a breadboard if you want the visual log.

## Hot-swap caveat

The TinyUSB host stack on RP2-family MCUs can occasionally get stuck after the upstream device is unplugged and fail to re-enumerate on re-plug. A 3 s watchdog in `feather_bridge::task` works around this: when the upstream side has been gone for `MIDI2_CPP_BRIDGE_WATCHDOG_MS`, the host side is reset (`tuh_deinit` + `tusb_init`). Default 3000 ms, tunable at compile time:

```bash
cmake -B build -DMIDI2_CPP_BRIDGE_WATCHDOG_MS=5000   # 5 s
cmake -B build -DMIDI2_CPP_BRIDGE_WATCHDOG_MS=0      # disable
```

## Validation

Pair this bridge with the device-side example we ship:

- [`waveshare-rp2350-usb-a-midi2`](../waveshare-rp2350-usb-a-midi2) is the device variant of this same board; flash it on a second RP2350-USB-A and connect the two boards via their USB-A and USB-C ports for a same-family round trip.
- [`rp2040-midi2`](../rp2040-midi2), [`waveshare-rp2040-midi2`](../waveshare-rp2040-midi2) work as upstream devices over a USB-C-male to USB-A-male cable.
- Any commercial MIDI 2.0 keyboard, controller, or interface with a Type-A or Type-B upstream connector also works.

For PC-side validation, plug the bridge USB-C into a laptop and inspect the enumeration with [Microsoft MIDI Services Console](https://github.com/microsoft/MIDI) on Windows, `amidi -l` on Linux, or Audio MIDI Setup on macOS. Expected:

- Native data format: Universal MIDI Packet
- Protocol: Midi2
- MIDI 2.0 Protocol: True
- Name: `waveshare-RP2350-USB-A bridge`
- USB VID / PID: `CAFE / 4077`

### Bench setup

![bench top-down with the Waveshare RP2350-USB-A wired up](monitor/stack.png)
![laptop running Microsoft MIDI Services Console next to the bench](monitor/bridge.png)
![Microsoft MIDI Services Console message log](monitor/windows.png)

## What lives where

```
midi2_cpp/
├── src/                            parent library (only midi2.c is consumed,
│                                   for midi2_msg_word_count)
└── examples/waveshare-rp2350-usb-a-bridge-midi2/
    ├── CMakeLists.txt              FetchContent for TinyUSB PR #3571 + Pico-PIO-USB
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
        ├── feather_bridge.{h,cpp}  dual TinyUSB init + task pump + cable→UMP
        ├── ump_router.{h,c}        single-threaded ring buffer (64 msgs/queue)
        ├── usb_descriptors.c       device descriptors (MIDI 2.0, 16 groups)
        ├── tusb_config.h           CFG_TUH + CFG_TUD enabled, both rhports
        ├── display.{h,c}           SSD1306 driver (optional, reused from feather host example)
        ├── font5x7.h               5x7 ASCII bitmap font (reused)
        └── main.cpp                showcase entry, bridge callbacks → display_log
```

The TinyUSB PR #3571 fork and Pico-PIO-USB are fetched at configure time into `build/_deps/` (gitignored). This example folder is under 4 MB on disk; the heaviest items are the bench photographs under `monitor/`.

## License

MIT, inherits the parent [`midi2_cpp` LICENSE](../../LICENSE). The TinyUSB fork (fetched on demand) is MIT (upstream by hathach, fork by sauloverissimo carrying the MIDI 2.0 class drivers from the still-open [PR #3571](https://github.com/hathach/tinyusb/pull/3571)). Pico-PIO-USB is MIT. The Waveshare RP2350-USB-A hardware reference assets bundled under `board/` (board photo, pinout, schematic) are © Waveshare Electronics, redistributed for documentation purposes. The R13 hardware modification reference and photographs at qsantos.fr are © Quentin Santos.

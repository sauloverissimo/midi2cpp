# [midi2_cpp](../..) | Device MIDI 2.0
## LilyGo T-Display S3 (receiver, on-board piano roll)

USB MIDI 2.0 device receiver for the **LilyGo T-Display S3** (ESP32-S3R8, 8 MB Octal PSRAM, 16 MB flash, ST7789 1.9" 320x170 IPS parallel 8-bit). Headless on the audio side, visual on the display side: the host sends UMP, the on-board piano roll mirrors note activity in real time. Lives at `midi2_cpp/examples/t-display-s3-midi2/` and consumes the parent library directly (no vendoring).

![t-display-s3-midi2 banner, LilyGo T-Display S3 board photo](board/banner.jpg)

> ⚠️ **TinyUSB override, not yet upstream.** The USB MIDI 2.0 device class driver this project depends on lives in TinyUSB [PR #3571](https://github.com/hathach/tinyusb/pull/3571), still under review. Until that PR merges into `hathach/tinyusb`, this build pulls a personal fork ([`sauloverissimo/tinyusb` branch `feat/midi2-device-host-driver`](https://github.com/sauloverissimo/tinyusb/tree/feat/midi2-device-host-driver)) at a pinned SHA into `idf/external/tinyusb`, registered as the ESP-IDF component `tinyusb` through the shim at `idf/components/tinyusb`. Treat the build as **beta**: when the PR lands upstream the override goes away and this README will point at the official TinyUSB.

PID `0x4094` distinguishes this device from the others; a host enumerating all `midi2_cpp` examples on the same machine sees distinct endpoints.

## What this is

`t-display-s3-midi2` is a **receiver showcase**: it does not emit notes, it does not generate sound, it does not synthesise audio. Its job is to be a well-formed USB MIDI 2.0 device, accept whatever a host (DAW, OS, sibling host recipe) sends over UMP, and visualise the activity on the on-board ST7789 display.

The recipe owns:

- ESP-IDF v5.4 board init: USB-OTG internal PHY in device mode, ST7789 panel power gate (GP15), LCD parallel-8-bit driver via LovyanGFX, 8 MB Octal PSRAM enabled (the full-screen sprite is ~108 KB and lives in PSRAM).
- TinyUSB MIDI 2.0 device class wiring via the **PR #3571 fork**, dropped into `idf/external/tinyusb` by the bootstrap script and registered as `idf/components/tinyusb`.
- USB descriptors (VID `0xCAFE`, PID `0x4094`, Product `TDisplayS3`).
- The five [midi2_cpp](https://github.com/sauloverissimo/midi2_cpp) platform hooks already wired: `setWriteFn`, `feedRx`, `setNowFn`, `setMounted` / `setAltSetting`, `CI::setRngFn`.
- A `piano_display` ESP-IDF component that renders a 25-key piano roll plus an info bar (identity, USB lifecycle, per-category UMP counters) at ~60 fps from a dedicated FreeRTOS task pinned to core 1. The TinyUSB device task runs on core 0.

After `t_display_s3_midi2::init(midi, ci)` the application sees only `m2device` and `m2ci`. It never touches `tud_*`, `esp_*`, the LovyanGFX driver, or any USB symbol. The `piano_display::set_note_active(note, on)` call is the only bridge between inbound UMP and what shows on the screen.

## What this is not

Not a finished product. The bundled `t-display-s3-midi2-receiver` executable is a **demo application** built on top of the receiver core: it reacts to inbound MIDI 2.0 NoteOn / NoteOff (full 16-bit velocity) plus MIDI 1.0 NoteOn / NoteOff (passthrough on the same UMP stream) and lights piano keys accordingly. Real applications copy this core and replace the receiver behaviour with their own logic:

- `t-display-s3-tuner`, swaps the piano roll for a chromatic tuner driven by inbound CC + Pitch Bend.
- `t-display-s3-controller`, adds the two on-board buttons (GP0 BOOT, GP14) as MIDI 2.0 emitters and uses the display for live UI feedback.
- *(your project here)*

## Identification

| Field | Value |
|---|---|
| USB VID | `0xCAFE` |
| USB PID | `0x4094` |
| USB Manufacturer | `github.com/sauloverissimo` |
| USB Product | `TDisplayS3` |
| Endpoint Name | `TDisplayS3` |
| Product Instance ID | `TDisplayS3-receiver-0001` |
| MIDI-CI Manufacturer ID | `{0x7D, 0x00, 0x00}` (MIDI Association educational/non-commercial prefix) |
| MIDI-CI Family / Model / Version | `0x0001 / 0x0001 / 0x00010000` |
| Function Block 0 | `Main`, bidirectional, group 1, MIDI 2.0 protocol declared |

> Reminder: VID `0xCAFE` is the TinyUSB educational placeholder, development-only. Forks targeting a real product MUST replace both `idVendor` and `idProduct` with their own allocation (`pid.codes 0x1209`, `Objective Development 0x16C0`, or a purchased USB-IF VID).

## Build

Requirements:

- **ESP-IDF v5.4 or newer** with the export script sourced (`. $IDF_PATH/export.sh`).
- A LilyGo T-Display S3 (the recipe targets the **8 MB PSRAM Octal** variant, ESP32-S3R8 silicon).
- A USB-C cable to the board's single USB-C connector.
- **Internet on the first run** (the bootstrap script clones the TinyUSB fork; LovyanGFX 1.2.0 is fetched from GitHub via the ESP-IDF Component Manager).

```bash
git clone https://github.com/sauloverissimo/midi2_cpp.git
cd midi2_cpp/examples/t-display-s3-midi2/idf
./scripts/fetch_tinyusb.sh         # one-off, clones the fork at the pinned SHA
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash       # see "Flash" below for the watchdog_reset note
```

### Override TinyUSB with a local working copy

```bash
ln -sfn /path/to/your/tinyusb idf/external/tinyusb
idf.py reconfigure
```

### Flash, USB-Serial-JTAG quirk

The T-Display S3 has a **single USB-C port** wired to the ESP32-S3 USB-OTG peripheral. Before the firmware is running the chip exposes the USB-Serial-JTAG ROM bootloader (`/dev/ttyACM0` on Linux); after enumeration the same port becomes the USB MIDI 2.0 device. There is no on-board USB-to-UART bridge (no CP2102), so flashing always goes through the JTAG endpoint.

`Hard resetting via RTS pin...` is **fake** on USB-Serial-JTAG (no real DTR/RTS lines), so `idf.py flash` alone leaves the chip parked in ROM after the flash finishes. Force a watchdog reset to boot the freshly-flashed app:

```bash
idf.py -p /dev/ttyACM0 flash
python -m esptool --chip esp32s3 -p /dev/ttyACM0 --after watchdog_reset run
```

After the watchdog reset the USB-C port is no longer `/dev/ttyACM0`; the firmware is now running and the host enumerates the device as USB MIDI 2.0 (VID:PID `0xCAFE:0x4094`).

If the chip refuses to enter download mode automatically, hold **GP0 (BOOT)** while plugging in the cable, release once flashing starts.

### Console / serial logs

The T-Display S3 has no on-board USB-to-UART bridge and the single USB-C port is owned by TinyUSB at runtime, so console output is wired to **UART0** (TX `GP43`, RX `GP44`) on the side header. Connect an external USB-TTL adapter to those pins to watch the boot log; they expose the same `ESP_LOGI(...)` lines documented in the Validation section. The default baud is `115200 8N1`.

## Hardware

| Pin / signal | Use |
|---|---|
| USB-C | Native USB-OTG, MIDI 2.0 device interface (also the flash path via USB-Serial-JTAG ROM before the app runs). |
| UART0 TX (GP43) / RX (GP44) | Console stdio at 115200 8N1. Wire to an external USB-TTL adapter to watch logs. |
| GP15 | LCD VDD power-enable. Driven HIGH by `piano_display::init()` before the LovyanGFX panel init runs; without this the display stays dark even with the backlight PWM up. |
| GP6 / GP7 / GP5 | LCD CS / RS-DC / RST. |
| GP8 / GP9 | LCD WR / RD strobes (parallel-8-bit bus). |
| GP39..42, GP45..48 | LCD D0..D7 parallel data lines. |
| GP38 | LCD backlight (PWM, channel 7, 22 kHz). |
| GP0 | BOOT button. Hold during reset/plug-in to enter download mode. Also reachable from firmware as a user button. |
| GP14 | User button (KEY1 on the silkscreen). Reserved for future controller variants. |
| RST | Reset / power cycle. |

PSRAM Octal at 80 MHz is mandatory in `sdkconfig.defaults`; the full-screen 320x170 16-bpp sprite is ~108 KB, larger than the SRAM headroom comfortable in this firmware mix. The board variant is the **ESP32-S3R8** module (8 MB Octal PSRAM); the older R8N16 with Quad PSRAM does not work with the supplied `sdkconfig.defaults` without flipping `CONFIG_SPIRAM_MODE_QUAD=y`.

ST7789 silkscreen reference and pin layout: see [`board/pinout.jpg`](board/pinout.jpg).

The MCU silicon datasheet is shared across every ESP32-S3 recipe and changes infrequently; read it on Espressif's site: [ESP32-S3 series datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf). Board details (schematic, mechanical, factory firmware) live in [LilyGo's T-Display-S3 repo](https://github.com/Xinyuan-LilyGO/T-Display-S3).

## Spec coverage

**Tier A** (full receiver-side UMP coverage). The recipe is a device-side responder; the table below lists what the device decodes and reacts to. The Showcase section then describes how each decoded category lands on the display.

### What this recipe decodes (inbound) and responds to (outbound)

| UMP MT | Direction | Spec section | Showcase reaction | Notes |
|---|---|---|---|---|
| 0x0 Utility | RX (host->dev) + TX (heartbeat) | M2-104-UM §3 | counter, no UI footprint | JR Timestamp echoed by midi2_cpp; device emits its own 500 ms heartbeat |
| 0x2 MIDI 1.0 Channel Voice | RX | M2-104-UM §5 | piano key lit / unlit; counter | NoteOn/Off, CC, PitchBend bumped on the info bar |
| 0x4 MIDI 2.0 Channel Voice | RX | M2-104-UM §7 | piano key lit / unlit (16-bit velocity preserved); counter | full subtype set: NoteOn/Off, Poly KP, CC, RPN, NRPN, Relative, Program+Bank, Channel KP, PB, Per-Note PB, Per-Note Mgmt, Note Attribute |
| 0x3 Data 64 (SysEx7) | RX | M2-104-UM §6 | counter (Other) | reassembled by the library, application has access via `m2device::onSysEx7` (not used by the showcase) |
| 0x5 Data 128 (SysEx8) | RX | M2-104-UM §8 | counter (Other) | same, available via `m2device::onSysEx8` |
| 0xD Flex Data | RX | M2-104-UM §9 | counter (Other) | available via `m2device::onFlexData`, the recipe consumes them but does not display |
| 0xF UMP Stream | TX (responder) | M2-104-UM §10 | (no UI) | Endpoint Discovery, Endpoint Info, Endpoint Name, Product Instance ID, Stream Config Notify, FB Discovery, FB Info, FB Name |

### MIDI-CI surface (M2-101-UM)

| Subsystem | Coverage |
|---|---|
| Discovery (Initiator + Responder) | responder: yes (MUID, Manufacturer, Family, Model, Version, MaxSysEx, Categories) |
| Profile Configuration | responder: List Profile + 1 custom Profile registered (`7D 00 00 01 00`) |
| Property Exchange | responder: Capability + Get on `DeviceInfo` (static) |
| Process Inquiry | responder: Capability declared via `setMidiReport` (system + channel + note bitmaps) |

### What this recipe does NOT cover (and why)

- **Outbound MIDI 2.0 channel-voice emission**: the recipe is a receiver showcase; the emission side is exercised by [`rp2040-midi2`](../rp2040-midi2) and [`esp32-s3-devkitc-usb-midi2`](../esp32-s3-devkitc-usb-midi2). Pair with one of those for round-trip validation.
- **PE Notify broadcast / Subscription state**: the receiver only declares static `DeviceInfo`; a controller variant would add subscribable PE properties.
- **Mixed Data Set full transfer**: out of scope. The parent library supports it (`midi2_cpp/docs/coverage.md`); a future `*-mds-*` recipe will exercise it.
- **MIDI-CI Initiator role**: this is a device-side responder. An Initiator demo lives in the future ESP32 host recipes.

## Showcase

What the bundled `t-display-s3-midi2-receiver` executable does after enumeration. The recipe is reactive: it has no scene timeline of its own, it visualises what the host pushes.

**Always-on (boot to forever):**

- **JR Timestamp heartbeat** every 500 ms (MT 0x0 status 0x2), keeps Linux ALSA polling alive on idle endpoints.
- **UMP Stream Discovery responder** (MT 0xF), replies to host Endpoint Discovery and FB Discovery with the full set.
- **MIDI-CI Discovery + PE Capability + PE Get** auto-replied via `m2ci`'s Appendix E convenience responder.
- **Piano render task** at ~60 fps on core 1, redraws the 25-key roll + the info bar from the active-note buffer + counter atomics.

**On host activity:**

| Event | Display reaction |
|---|---|
| MIDI 2.0 NoteOn (MT 0x4) | piano key lit (cyan if white, warm orange if black); `On` counter increments |
| MIDI 2.0 NoteOff (MT 0x4) | piano key unlit; `Off` counter increments |
| MIDI 1.0 NoteOn / NoteOff (MT 0x2) | same, the recipe wires both event types into the same active-note buffer |
| Note out of view | the piano auto-shifts so the active region centres on the new note; out-of-view active notes show as a red triangle (below) or blue triangle (above) on the info bar edges |
| CC | `CC` counter increments, no per-key effect |
| Pitch Bend | `PB` counter increments |
| Anything else (PE, SysEx, Flex Data) | `Other` counter increments |
| Host disconnect | status banner flips to "waiting for host..."; active notes stay lit until a NoteOff arrives or the user reboots |

The info bar reads `TDisplayS3   MIDI 2.0 RX   <status>` on the top row, then `On <n>  Off <n>  CC <n>  PB <n>  Other <n>` underneath, plus a `<low note>-<high note>` range label and the out-of-view triangles.

## Validation

Hardware steps:

1. Flash the recipe via the USB-C connector (see `## Build / Flash, USB-Serial-JTAG quirk`).
2. After the watchdog reset the device enumerates as USB MIDI 2.0:
   - **Linux**: `lsusb | grep cafe:4094` shows `TDisplayS3`. `amidi -l` lists `Group 1 (Main)`.
   - **Windows**: Microsoft MIDI Services Console shows `TDisplayS3` with Native data format = UMP, MIDI 2.0 Protocol = True.
   - **macOS**: Audio MIDI Setup shows `TDisplayS3` with one source / one destination, MIDI 2.0 Protocol declared.
3. Wire the **UART0 console** (GP43 TX / GP44 RX) to an external USB-TTL adapter at 115200 8N1; you should see `boot` / `stream` / `rx` log lines as soon as the device mounts.
4. Drive notes from any MIDI 2.0 host:
   - **Pico SDK device** [`rp2040-midi2`](../rp2040-midi2) plugged into the **same** PC: the host's MIDI patchbay routes its output to this T-Display S3, the showcase's full velocity ramps light keys with the cyan / warm-orange palette of the receiver.
   - **Microsoft MIDI Services Console** "send messages" panel: type a NoteOn UMP word, watch the corresponding piano key light up.
   - **`amidi -p hw:N,1,0 -S "903C7F"`** on Linux sends a MIDI 1.0 NoteOn (C4 at velocity `7F`) into the MIDI 2.0 alt setting; ALSA upscales the byte stream into UMP MT 0x2 and the receiver lights the centre key of the piano roll. `hw:N,1,0` is the MIDI 2.0 subdevice listed by `amidi -l`; subdevice 0 is the MIDI 1.0 fallback alt.
5. The on-board **piano roll redraws every frame**, visually confirming the recipe is reacting to the host's stream end-to-end.

This recipe is the **receiver / visualiser** side of the ESP32-S3 family. The natural sibling is [`esp32-s3-devkitc-usb-midi2`](../esp32-s3-devkitc-usb-midi2), a headless emitter on the same MCU running the full Tier A Showcase: plug both into the same host, route the DevKitC's output to the T-Display in the host's MIDI patchbay, and the piano roll mirrors every Scene of the Showcase live. Cross-platform pairing options: [`rp2040-midi2`](../rp2040-midi2) as a clean Pico SDK MIDI 2.0 emitter, or [`adafruit-feather-rp2040-bridge-midi2`](../adafruit-feather-rp2040-bridge-midi2) when you want a third-party MIDI 2.0 controller routed through a bridge before reaching the T-Display.

### Bench setup

![T-Display S3 wired into a laptop running the host recipe; the on-board ST7789 piano roll lights keys in sync with the inbound UMP stream](monitor/stack.png)

![Microsoft MIDI Services Console message log driving the receiver: NoteOn / NoteOff rows interleaved with JR Timestamp ticks; T-Display piano roll on the bench mirrors the active note in real time](monitor/windows.png)

## What lives where

```
midi2_cpp/
├── src/                            parent library (consumed by this example
│                                   via ../../../src in idf/main/CMakeLists.txt)
└── examples/t-display-s3-midi2/
    ├── README.md
    ├── board/
    │   ├── banner.jpg              repo banner (T-Display S3 board photo)
    │   └── pinout.jpg              ST7789 + ESP32-S3 pinout reference
    ├── monitor/                    bench / Microsoft MIDI Console captures (TBD)
    └── idf/
        ├── CMakeLists.txt          ESP-IDF project root
        ├── partitions.csv          single-app, 16 MB flash, 3 MB factory
        ├── sdkconfig.defaults      target esp32s3, PSRAM Octal 80 MHz, UART console
        ├── scripts/
        │   └── fetch_tinyusb.sh    bootstrap: clones TinyUSB fork into external/tinyusb
        ├── external/                (gitignored, populated by fetch_tinyusb.sh)
        │   └── tinyusb/             raw clone of the PR #3571 fork at pinned SHA
        ├── components/
        │   ├── tinyusb/
        │   │   ├── CMakeLists.txt   shim: registers fork sources as ESP-IDF "tinyusb"
        │   │   └── usb_descriptors.c   PID 0x4094, Product "TDisplayS3"
        │   └── piano_display/
        │       ├── CMakeLists.txt   ESP-IDF component, depends on lovyangfx
        │       ├── idf_component.yml   pulls LovyanGFX 1.2.0 from GitHub
        │       ├── include/piano_display.h    public API
        │       └── piano_display.cpp          ST7789 panel + render loop
        └── main/
            ├── CMakeLists.txt      idf_component_register, pulls midi2_cpp from ../../../../src
            ├── idf_component.yml   managed deps (idf >=5.4)
            ├── tusb_config.h       1 group, 1 function block, FS USB-OTG
            ├── t_display_s3_midi2.h    public API of the platform glue (init/task/show_mounted)
            ├── t_display_s3_midi2.cpp  USB-OTG PHY init + TinyUSB task + piano render task
            └── main.cpp            receiver entry, UMP -> piano UI bridge
```

The TinyUSB PR #3571 fork is dropped into `idf/external/tinyusb` (gitignored) by `idf/scripts/fetch_tinyusb.sh`. The shim component at `idf/components/tinyusb/` registers a curated subset of the fork's sources (tusb core + device stack + MIDI 2.0 class driver + DWC2 DCD) as an ESP-IDF component named `tinyusb`, plus the recipe's `usb_descriptors.c` so the `tud_descriptor_*_cb` symbols sit in the same archive as `usbd.c` (the linker would otherwise drop them).

LovyanGFX is pulled by the ESP-IDF Component Manager directly from upstream GitHub (`lovyan03/LovyanGFX` tag `1.2.0`), declared in `idf/components/piano_display/idf_component.yml`. The Espressif Component Registry does not list LovyanGFX, so the `git:` source is required.

## License

MIT, inherits the parent [`midi2_cpp` LICENSE](../../LICENSE). The TinyUSB fork (cloned on demand into `idf/external/tinyusb`) is MIT (upstream by hathach, fork by sauloverissimo carrying the MIDI 2.0 class drivers from the still-open [PR #3571](https://github.com/hathach/tinyusb/pull/3571)). LovyanGFX is FreeBSD-style permissive (see the upstream `LICENSE` file).

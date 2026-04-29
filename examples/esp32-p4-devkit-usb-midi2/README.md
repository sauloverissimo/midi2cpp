# [midi2_cpp](../..) | Device MIDI 2.0
## ESP32-P4-WIFI6-DEV-KIT

Full-spec USB MIDI 2.0 device example for the **Waveshare ESP32-P4-WIFI6-DEV-KIT**. Headless, full Showcase cycle of every MIDI 2.0 message category beyond MIDI 1.0, identical in behaviour to the [`esp32-s3-devkitc-usb-midi2`](../esp32-s3-devkitc-usb-midi2) sibling with the chip target swapped to ESP32-P4 (RISC-V, USB-OTG internal PHY) and the toolchain swapped to `riscv32-esp-elf`. Lives at `midi2_cpp/examples/esp32-p4-devkit-usb-midi2/` and consumes the parent library directly (no vendoring).

![esp32-p4-devkit-usb-midi2 banner, Waveshare ESP32-P4-WIFI6-DEV-KIT board photo](board/banner.jpg)

> ⚠️ **TinyUSB override, not yet upstream.** The USB MIDI 2.0 device class driver this project depends on lives in TinyUSB [PR #3571](https://github.com/hathach/tinyusb/pull/3571), still under review. Until that PR merges into `hathach/tinyusb`, this build pulls a personal fork ([`sauloverissimo/tinyusb` branch `feat/midi2-device-host-driver`](https://github.com/sauloverissimo/tinyusb/tree/feat/midi2-device-host-driver)) at a pinned SHA into `idf/components/tinyusb`. Treat the build as **beta**: when the PR lands upstream the override goes away and this README will point at the official TinyUSB.

PID `0x4091` distinguishes this device from the others; a host enumerating all `midi2_cpp` examples on the same machine sees distinct endpoints.

## What this is

`esp32-p4-devkit-usb-midi2` is the platform layer for a family of MIDI 2.0 devices on the ESP32-P4. It owns:

- ESP-IDF v5.4 board init (`usb_new_phy` with `USB_PHY_TARGET_INT`, device role, FreeRTOS task scheduler)
- TinyUSB MIDI 2.0 device class wiring via the **PR #3571 fork**, dropped into `idf/components/tinyusb` by the bootstrap script
- USB descriptors (VID `0xCAFE`, PID `0x4091`)
- The five [midi2_cpp](https://github.com/sauloverissimo/midi2_cpp) platform hooks already wired: `setWriteFn`, `feedRx`, `setNowFn`, `setMounted` / `setAltSetting`, `CI::setRngFn`

The Waveshare ESP32-P4-WIFI6-DEV-KIT exposes two USB-C jacks: the one labelled **USB-Device** routes the P4 internal PHY (used here for USB MIDI 2.0); the one labelled **ToUART** routes the CH343 USB-Serial-JTAG bridge for console + flashing. The board also has two USB-A jacks tied to the UTMI host PHY (not used in this device-only recipe; see the future `esp32-p4-devkit-host-midi2`).

After `esp32_p4_devkit_midi2::init(midi, ci)`, the application sees only `m2device` and `m2ci`. It never touches `tud_*`, `esp_*`, or any USB symbol. Replicating the same shape on another ESP32 board is a matter of writing `<board>_midi2.{h,cpp}` with the same two-function surface.

## What this is not

Not a finished product. The bundled `esp32-p4-devkit-usb-midi2-showcase` executable is a **demo application** built on top of this core: it exercises every category of UMP message MIDI 2.0 brings beyond MIDI 1.0, then loops. Real applications copy this core and replace the showcase with their own behaviour layer:

- `esp32-p4-devkit-player`, adds an SMF parser + I2S audio playback engine driving the on-board speaker connector
- `esp32-p4-devkit-host-midi2`, swaps the device PHY for the UTMI host PHY on the USB-A jacks (host role)
- *(your project here)*

## Identification

| Field | Value |
|---|---|
| USB VID | `0xCAFE` |
| USB PID | `0x4091` |
| USB Manufacturer | `github.com/sauloverissimo` |
| USB Product | `ESP32P4DevKit` |
| Endpoint Name | `ESP32P4DevKit` |
| Product Instance ID | `ESP32P4DevKit-showcase-0001` |
| MIDI-CI Manufacturer ID | `{0x7D, 0x00, 0x00}` (MIDI Association educational/non-commercial prefix) |
| MIDI-CI Family / Model / Version | `0x0001 / 0x0001 / 0x00010000` |

## Build

Requirements:

- **ESP-IDF v5.4 or newer** with the export script sourced (`. $IDF_PATH/export.sh`)
- The matching RISC-V toolchain (`riscv32-esp-elf`); run `$IDF_PATH/install.sh esp32p4` once on a fresh IDF
- A Waveshare ESP32-P4-WIFI6-DEV-KIT, two USB-C cables (one to the **ToUART** jack for flashing + console, one to the **USB-Device** jack for MIDI 2.0)
- Internet on the first run (the bootstrap script clones the TinyUSB fork)

```bash
git clone https://github.com/sauloverissimo/midi2_cpp.git
cd midi2_cpp/examples/esp32-p4-devkit-usb-midi2/idf
./scripts/fetch_tinyusb.sh         # one-off, ~36 MB clone of the fork at pinned SHA
. $IDF_PATH/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor    # ToUART jack on the Waveshare kit
```

The Waveshare kit's "ToUART" USB-C jack uses a **CH343 USB-Serial-JTAG bridge** (VID `1a86:55d3`) which the Linux mainline kernel binds to `cdc_acm`, so `/dev/ttyACM0` is the natural device node. The CH343 has real DTR/RTS, so `idf.py flash` auto-resets the chip into download mode without a button press.

### Flashing via the native USB-Device jack

The native USB-OTG ("USB-Device") jack also exposes the P4 USB-Serial-JTAG ROM bootloader as a second `/dev/ttyACM*` before the app firmware claims that controller. Flashing through it works, but `Hard resetting via RTS pin...` is **fake** on USB-Serial-JTAG (no real DTR/RTS); the chip stays in ROM after the flash finishes. Force a watchdog reset to boot the freshly-flashed app:

```bash
idf.py -p /dev/ttyACM<N> flash       # whatever node the USB-Device jack maps to
python -m esptool --chip esp32p4 -p /dev/ttyACM<N> --after watchdog_reset run
```

After the watchdog reset the USB-Device jack is no longer `ttyACM*`; the firmware is now running and the host enumerates the device as USB MIDI 2.0 (VID:PID `0xCAFE:0x4091`).

### Override TinyUSB with a local working copy

```bash
ln -sfn /path/to/your/tinyusb idf/external/tinyusb
idf.py reconfigure
```

## Hardware

| Connector / Pin | Use |
|---|---|
| USB-C "USB-Device" | Native USB-OTG (internal PHY, device role), MIDI 2.0 device interface |
| USB-C "ToUART" | CH343 USB-Serial-JTAG bridge, console stdio @ 115200 8N1 + flashing |
| USB-A jacks (×2) | UTMI host PHY, **not used** in this device-only recipe |
| RJ45 | Ethernet, not used |
| Speaker JST | I2S audio out, not used (future `*-player` recipe) |
| MIPI-CSI ribbon | Camera, not used |
| MIPI-DSI ribbon | Display, not used |
| BOOT button | Hold during reset to enter download mode (rarely needed; CH343 auto-reset handles it) |
| RESET button | Reboot |

## Spec coverage

**Tier A** (full UMP showcase). Reference target for `midi2_cpp` ESP-IDF recipes; same coverage as the Pico SDK `rp2040-midi2`.

### What this recipe emits and demonstrates

| UMP MT | Transport | Spec section | Showcase Scene | Notes |
|---|---|---|---|---|
| 0x0 Utility | USB | M2-104-UM §3 | JR heartbeat, Scene H | 500 ms periodicity; DCTPQ + Delta Clockstamp in Scene H |
| 0x2 MIDI 1.0 Channel Voice | USB | M2-104-UM §5 | (passthrough only) | available via `sendXxx` for downscale apps |
| 0x3 Data 64 (SysEx7) | USB | M2-104-UM §6 | (available, not in showcase) | reachable via `sendSysEx` |
| 0x4 MIDI 2.0 Channel Voice | USB | M2-104-UM §7 | A, B, C, D, E, F | every subtype: NoteOn/Off, Poly KP, CC, RPN, NRPN, Relative RPN/NRPN, Program+Bank, Channel KP, PB, PNPB, PN Mgmt, Note Attribute |
| 0x5 Data 128 (SysEx8) | USB | M2-104-UM §8 | G | 16-byte raw 8-bit payload, no 7-bit aliasing |
| 0xD Flex Data | USB | M2-104-UM §9 | A | Tempo, Time Sig, Key Sig, Metronome, Chord Name, Start of Clip, End of Clip |
| 0xF UMP Stream | USB | M2-104-UM §10 | (responder, not a Scene) | Endpoint Discovery, Endpoint Info, Endpoint Name, Product Instance ID, Stream Config Notify, FB Discovery, FB Info, FB Name |

### MIDI-CI surface (M2-101-UM)

| Subsystem | Coverage |
|---|---|
| Discovery (Initiator + Responder) | responder: yes (MUID, Manufacturer, Family, Model, Version, MaxSysEx, Categories) |
| Profile Configuration | responder: List Profile + 1 custom Profile registered (`7D 00 00 01 00`) |
| Property Exchange | responder: Capability + Get on `DeviceInfo` (static), `ChannelList` (dynamic), `OverlayRate` (subscribable, broadcast Notify each cycle) |
| Process Inquiry | responder: Capability declared via `setMidiReport` (system + channel + note bitmaps) |

### What this recipe does NOT cover (and why)

- **Mixed Data Set full transfer (MT 0x5/0x8/0x9/0xC)**: out of scope for the headless showcase; the parent library supports it (see `midi2_cpp/docs/coverage.md`). Future `*-mds-*` recipes will exercise it.
- **MIDI-CI Profile Specific Data inbound handling**: responder only acknowledges; profile state is application logic.
- **MIDI 2.0 Initiator role for CI**: this is a device-side responder; an Initiator demo lives in the future ESP32 host recipes.

## Showcase

What the bundled `esp32-p4-devkit-usb-midi2-showcase` executable demonstrates after enumeration. Constants in [`idf/main/main.cpp`](idf/main/main.cpp), adjust to taste. Each cycle is ~22 s and loops continuously while the device stays mounted.

**Always-on (boot to forever):**

- **JR Timestamp heartbeat** every 500 ms (MT 0x0 status 0x2), keeps Linux ALSA polling alive on idle endpoints
- **UMP Stream Discovery responder** (MT 0xF), replies to host Endpoint Discovery and FB Discovery with the full set
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

Every scene logs to UART via the CH343 bridge on the **ToUART** USB-C jack at 115200 8N1 so a serial monitor (`cat /dev/ttyACM0` after `stty 115200`, or `idf.py monitor`) lets you watch the timeline live.

## Validation

Hardware steps:

1. Cable both USB-C jacks to the host. The **USB-Device** jack carries the MIDI 2.0 traffic; the **ToUART** jack carries the console + flashing path.
2. Flash via the ToUART jack: `idf.py -p /dev/ttyACM0 flash monitor` (CH343 has real DTR/RTS, auto-reset works).
3. On the host, confirm enumeration:
   - **Linux**: `lsusb | grep cafe:4090` shows `ESP32P4DevKit`. `amidi -l` lists `Group 1 (Main)`. `aseqdump -p <port>` shows the showcase events live.
   - **Windows**: Microsoft MIDI Services Console shows `ESP32P4DevKit` with Native data format = UMP, MIDI 2.0 Protocol = True.
   - **macOS**: Audio MIDI Setup shows `ESP32P4DevKit`. Bonjour mDNS shows the device.
4. Watch the showcase: every ~22 s the UART logs `[cycle N] start` and the scenes A through J fire. Sample UART log:
   ```
   [B] Assignable Per-Note Controller #74 (brightness) val=0xA0000000
   [B] Per-Note Management Reset
   [C] step 7 note=79 vel=0xFFF9 cc74=0xFFFFFFF9 pb=0xFFFFFFFE poly=0xFFFFFFFC chp=0xFFFFFFFD
   ```
5. Captured UMP via `aseqdump -p <client>:1` shows ALSA's MIDI 1.0 downscaling of the same stream (the host kernel does the UMP to byte-stream conversion when user-space tooling does not speak UMP natively).

This recipe is the device side of the future ESP32 host/bridge pair. Until those ship, pair with the Pico SDK host counterpart [`adafruit-feather-rp2040-host-midi2`](../adafruit-feather-rp2040-host-midi2) for a cross-platform sanity check.

### Bench setup

(Empty; bench / Microsoft MIDI Services Console captures will be embedded here once the user drops `monitor/stack.png`, `monitor/properties.png`, `monitor/windows.png` after the hardware test. See [esp32-s3-devkitc-usb-midi2](../esp32-s3-devkitc-usb-midi2#bench-setup) for the canonical layout this section follows.)

## What lives where

```
midi2_cpp/
├── src/                            parent library (consumed by this example
│                                   via ../../../src in idf/main/CMakeLists.txt)
└── examples/esp32-p4-devkit-usb-midi2/
    ├── README.md
    ├── board/
    │   ├── banner.jpg              repo banner (copy of board.jpg until a real banner ships)
    │   ├── board.jpg               Waveshare top-down product photo
    │   ├── pinout-front.jpg        front view with peripheral connectors labelled
    │   ├── pinout-back.jpg         back view (BoM components)
    │   ├── pinout-side.jpg         side view (USB-C jacks + GPIO header)
    │   └── ESP32-P4-WIFI6-DEV-KIT-Datasheet.pdf
    ├── monitor/                    bench / Microsoft MIDI Console captures (TBD)
    └── idf/
        ├── CMakeLists.txt          ESP-IDF project root
        ├── partitions.csv          single-app, 8 MB flash
        ├── sdkconfig.defaults      target esp32p4, UART stdio, custom partition table
        ├── scripts/
        │   └── fetch_tinyusb.sh    bootstrap: clones TinyUSB fork into external/tinyusb
        ├── external/                (gitignored, populated by fetch_tinyusb.sh)
        │   └── tinyusb/             raw clone of the PR #3571 fork at pinned SHA
        ├── components/
        │   └── tinyusb/
        │       ├── CMakeLists.txt   shim: registers the fork's selected sources as
        │       │                    an ESP-IDF component named "tinyusb"
        │       └── usb_descriptors.c   PID 0x4091, Product "ESP32P4DevKit"
        │                            (lives here so tud_descriptor_*_cb sit in the
        │                            same archive as usbd.c, fixes link order)
        └── main/
            ├── CMakeLists.txt      idf_component_register, pulls midi2_cpp from ../../../../src
            ├── idf_component.yml   managed deps (led_strip)
            ├── tusb_config.h       1 group, 1 function block, FS USB-OTG
            ├── esp32_p4_devkit_midi2.h    public API of the platform glue
            ├── esp32_p4_devkit_midi2.cpp  USB-OTG PHY init + TinyUSB task + LED + hooks
            └── main.cpp            showcase entry, full-spec MIDI 2.0 demo
```

The TinyUSB PR #3571 fork is dropped into `idf/external/tinyusb` (gitignored) by `idf/scripts/fetch_tinyusb.sh`. The shim component at `idf/components/tinyusb/` registers a curated subset of the fork's sources (tusb core + device stack + MIDI 2.0 class driver + DWC2 DCD) as an ESP-IDF component named `tinyusb`, plus the recipe's `usb_descriptors.c` so the `tud_descriptor_*_cb` symbols sit in the same archive as `usbd.c` (the linker would otherwise drop them).

The board datasheet is bundled (`board/ESP32-P4-WIFI6-DEV-KIT-Datasheet.pdf`, 4 MB) because it carries the dev-kit-specific schematic excerpt and pinout that is hard to find elsewhere. The MCU silicon datasheet is not bundled; read it on Espressif's site: [ESP32-P4 series datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf).

## License

MIT, inherits the parent [`midi2_cpp` LICENSE](../../LICENSE). The TinyUSB fork (cloned on demand into `idf/components/tinyusb`) is MIT (upstream by hathach, fork by sauloverissimo carrying the MIDI 2.0 class drivers from the still-open [PR #3571](https://github.com/hathach/tinyusb/pull/3571)). The `led_strip` managed component is Apache 2.0 (Espressif).

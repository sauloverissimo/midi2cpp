# [midi2cpp](../..) | Bridge MIDI 2.0
## Waveshare RP2350-USB-A

Transparent USB MIDI 2.0 bridge on the **Waveshare RP2350-USB-A**. Runs TinyUSB host on USB-A (PIO-USB GP12 / GP13) and TinyUSB device on USB-C (native USB) in the same firmware, forwarding UMP between them so any MIDI 2.0 device plugged into USB-A appears on the PC as a 16-group MIDI 2.0 endpoint named `RP2350 USB-A Bridge MIDI 2.0`. Pico SDK build, no Arduino IDE.

![waveshare-RP2350-USB-A bridge banner](board/banner.png)

> **Hardware modification required.** The Waveshare RP2350-USB-A ships with a 1.5 kΩ pull-up resistor (`R13`) on the USB-A `D+` line. That pull-up biases the line for **device** mode; in **host** mode it prevents the RP2350 from detecting low-speed devices and hot-plug events. **`R13` must be desoldered before the bridge can enumerate anything on the USB-A port.** With this modification, the USB-A connector on this board can no longer be used as a device, only as a host. Photos and a step-by-step removal procedure: [Quentin Santos' write-up](https://qsantos.fr/2025/11/21/fixing-the-rp2350-usb-a-not-working-as-usb-host/).

## Topology

```
                                 ┌──────────────────────────────────┐
PC / DAW ───── USB-C ───────────►│ Waveshare RP2350-USB-A           │
                                 │   rhport 0 (native USB device)   │
                                 │      ▲                           │
                                 │      │ midi2::m2bridge             │
                                 │      ▼                           │
                                 │   rhport 1 (PIO-USB host, GP12/13)│
                                 └──────────────────────────────────┘
                                          ▲
                                          │ USB-A (R13 desoldered)
                                          │
                                  MIDI 2.0 device
                                  (or MIDI 1.0, uplifted)
```

One device slot (groups 1-4) plus the bridge's own Function Block (groups 5-16), where its MIDI-CI responder lives. The slot is identity-bound inside `midi2::m2bridge`; single-slot topology, so no persistence is needed.

USB-MIDI 1.0 uplift on the host side: upstream `alt=0` cable events become UMP MT 0x2 in the slot window, so the PC always sees clean MIDI 2.0.

## USB identity

What the PC sees on the device side (USB-C):

| Field | Value |
|---|---|
| VID:PID | `cafe:4077` (development-only) |
| Product | `RP2350 USB-A Bridge MIDI 2.0` |
| Manufacturer | `midi2.diy` |
| MIDI 2.0 Groups | 16 (1 slot of 4 + the bridge's own FB on groups 5-16) |
| Function Blocks | 2: the identity-bound device slot + the bridge itself |

## Build

Requires Pico SDK 2.x (RP2350 support is in 2.0+), `arm-none-eabi-gcc` (SDK auto-selects Cortex-M33), CMake 3.14+.

```bash
cmake -B build         # first run fetches TinyUSB + Pico-PIO-USB
cmake --build build -j
```

Pointing at local checkouts: `cmake -B build -DPICO_TINYUSB_PATH=/path/to/tinyusb -DPICO_PIO_USB_PATH=/path/to/Pico-PIO-USB`.

## Flash

Hold BOOT, plug USB-C, drag `build/waveshare-rp2350-usb-a-bridge-midi2-showcase.uf2` to the mounted RP2350 drive. Or `picotool load`.

## Hardware

![waveshare-RP2350-USB-A bridge banner](monitor/hardware.png)

| Pin | Use |
|---|---|
| USB-A jack | Host A-side (PIO-USB on GP12 D+ / GP13 D-, requires R13 desolder mod) |
| USB-C | Bridged MIDI 2.0 endpoint to the PC, programming + power (CDC stdio disabled) |
| GP2 / GP3 | I2C1 SDA / SCL (optional SSD1306 0x3C) |
| GP0 / GP1 | UART TX / RX debug print @ 115200 8N1 |

| Component | Use |
|---|---|
| 128x64 SSD1306 OLED | Optional, live forwarded UMP display |
| Upstream USB MIDI device | Source under test, UMP or USB-MIDI 1.0 |

The board has no software-controlled USB-A 5V power gate; VBUS comes through the USB-C connector and a poly fuse, with no firmware step required.

## Validation

Plug any USB MIDI 2.0 device into the USB-A jack, plug the USB-C into a PC. Expected on the PC:

- **Linux**: `lsusb | grep cafe:4077`. `amidi -l` lists the bridge's MIDI 2.0 group.
- **Windows**: Microsoft MIDI Services Console shows `RP2350 USB-A Bridge MIDI 2.0` with Native data format = UMP, MIDI 2.0 Protocol = True.
- **macOS**: Audio MIDI Setup shows `RP2350 USB-A Bridge MIDI 2.0`.

![bench top-down with the board on a protoboard](monitor/stack.png)
![laptop running Microsoft MIDI Services Console](monitor/bridge.png)


## Spec coverage

Bridge. Any message type from the upstream device is forwarded into the slot window except MT 0x0 (utility), 0xE (reserved) and 0xF (Stream), which are owned locally.

| UMP MT | Spec | Bridge behaviour |
|---|---|---|
| 0x0 Utility | M2-104-UM §7.2 | not forwarded; owned locally |
| 0x2 MIDI 1.0 Channel Voice | M2-104-UM §7.3 | forwarded with group rewrite (also the MIDI 1.0 alt 0 uplift destination) |
| 0x3 SysEx7 | M2-104-UM §7.7 | forwarded with group rewrite |
| 0x4 MIDI 2.0 Channel Voice | M2-104-UM §7.4 | forwarded with group rewrite |
| 0x5 SysEx8 / Mixed Data | M2-104-UM §7.8-7.9 | forwarded with group rewrite |
| 0xD Flex Data | M2-104-UM §7.5 | forwarded with group rewrite |
| 0xF UMP Stream | M2-104-UM §7.1 | not forwarded; bridge owns Endpoint + FB Discovery on both sides |

MIDI-CI: the slot window is exclusive, so PC MIDI-CI reaches the upstream device end to end; the bridge's own `m2ci` answers only on groups 5-16.

## Showcase

Three modes, switching automatically based on connectivity.

**`Waiting`** (no PC mount yet): splash + spinner.

**`Showcase`** (PC mounted, no upstream on USB-A): bridge emits its own UMP from the device side so a connected DAW can validate the link without an upstream.

- Chromatic walk C4 to B4: NoteOn / Off every 250 ms (24 steps total, MT 0x4, group 5 [the bridge FB], ch 0, vel `0xC000`)
- CC #74 (Brightness) 32-bit sweep every 6 s (5 points across the 32-bit range)

**`Bridging`** (PC mounted, upstream on USB-A): showcase pauses, forward path takes over.

- Upstream UMP flows to the PC in the slot window (groups 1-4)
- PC UMP flows to the upstream when the upstream is MIDI 2.0 (alt=1)
- USB-MIDI 1.0 upstream cable events uplifted to UMP MT 0x2

UART debug on GP0 mirrors mount events.

## Scope and limitations

- **Single upstream device** at a time (one slot). A second device plugged in is enumerated by TinyUSB but not placed; no traffic flows for it.
- **MIDI 1.0 uplift is one-way**: upstream cable events become UMP MT 0x2 in the slot window. PC-to-upstream traffic reaches MIDI 2.0 upstreams only.
- **Group windows**: upstream traffic lands on groups 1-4 (the slot window); the bridge's own MIDI-CI answers on groups 5-16.

## Hot-swap caveat

A 3 s watchdog in `feather_bridge::task` resets the host side (`tuh_deinit` + `tusb_init`) after the upstream device has been gone for `MIDI2CPP_BRIDGE_WATCHDOG_MS`. Tune at compile time:

```bash
cmake -B build -DMIDI2CPP_BRIDGE_WATCHDOG_MS=5000   # 5 s
cmake -B build -DMIDI2CPP_BRIDGE_WATCHDOG_MS=0      # disable
```

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE). Pico-PIO-USB is MIT. Waveshare hardware reference assets under `board/` (board photo, pinout, schematic) are © Waveshare Electronics. The R13 hardware modification reference and photographs at qsantos.fr are © Quentin Santos.

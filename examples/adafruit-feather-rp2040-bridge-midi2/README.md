# [midi2cpp](../..) | Bridge MIDI 2.0
## Adafruit Feather RP2040 USB Bridge

Transparent USB MIDI 2.0 bridge on the **Adafruit Feather RP2040 USB Host**. Runs TinyUSB host on USB-A (PIO-USB GP16 / GP17) and TinyUSB device on USB-C (native USB) in the same firmware, forwarding UMP between them so any MIDI 2.0 device plugged into USB-A appears on the PC as a 16-group MIDI 2.0 endpoint named `rp2040-midi2 bridge`. Pico SDK build, no Arduino IDE.

![adafruit-feather-rp2040-bridge-midi2 banner](board/banner.png)

## Topology

```
                                 ┌──────────────────────────────────┐
PC / DAW ───── USB-C ───────────►│ Feather RP2040 USB Host          │
                                 │   rhport 0 (native USB device)   │
                                 │      ▲                           │
                                 │      │ ump_router (1 msg/iter)   │
                                 │      ▼                           │
                                 │   rhport 1 (PIO-USB host, GP16/17)│
                                 └──────────────────────────────────┘
                                          ▲
                                          │ USB-A
                                          │
                                  MIDI 2.0 device
                                  (or MIDI 1.0, uplifted)
```

USB-MIDI 1.0 uplift on the host side: upstream `alt=0` cable events (CIN 0x8..0xE) become UMP MT 0x2 so the PC always sees clean MIDI 2.0.

## USB identity

What the PC sees on the device side (USB-C):

| Field | Value |
|---|---|
| VID:PID | `cafe:4071` (development-only) |
| Product | `rp2040-midi2 bridge` |
| Manufacturer | `github.com/sauloverissimo` |
| MIDI 2.0 Groups | 16 (1:1 passthrough, group N upstream becomes group N to PC) |
| Function Blocks | 1 (covers all groups) |

## Build

Requires Pico SDK 2.x (with `PICO_SDK_PATH` exported), `arm-none-eabi-gcc`, CMake 3.14+.

```bash
cmake -B build         # first run fetches TinyUSB + Pico-PIO-USB
cmake --build build -j
```

Pointing at local checkouts: `cmake -B build -DPICO_TINYUSB_PATH=/path/to/tinyusb -DPICO_PIO_USB_PATH=/path/to/Pico-PIO-USB`.

## Flash

Hold BOOTSEL on the Feather, plug USB-C, drag `build/adafruit-feather-rp2040-bridge-midi2-showcase.uf2` to the mounted `RPI-RP2` drive. Or `picotool load build/adafruit-feather-rp2040-bridge-midi2-showcase.uf2 -fx`.

## Hardware

![adafruit-feather-rp2040-bridge-midi2 banner](board/banner2.png)

| Pin | Use |
|---|---|
| USB-A jack | Host A-side (PIO-USB on GP16 D+ / GP17 D-, 5V power gate on GP18 driven HIGH at init) |
| USB-C | Bridged MIDI 2.0 endpoint to the PC, programming + power (CDC stdio disabled) |
| GP2 / GP3 | I2C1 SDA / SCL (SSD1306 0x3C on STEMMA QT) |
| GP0 / GP1 | UART TX / RX debug print @ 115200 8N1 |

| Component | Use |
|---|---|
| 128x64 SSD1306 OLED | Live forwarded UMP, on STEMMA QT (I2C 0x3C) |
| Upstream USB MIDI device | Source under test, UMP or USB-MIDI 1.0 |

## Validation

Plug any USB MIDI 2.0 device into the USB-A jack, plug the USB-C into a PC. The PC should enumerate `cafe:4071 rp2040-midi2 bridge` as a 16-group MIDI 2.0 endpoint. The OLED should print `>` markers for upstream→PC events and `<` markers for PC→upstream events.

![bridge running on protoboard](monitor/prototype.png)
![Daisy upstream identity in Microsoft MIDI Console](monitor/windows_1.png)
![Microsoft MIDI Services Console message log](monitor/windows_2.png)
![bench setup with upstream device and Windows monitor](monitor/stack.png)

## Spec coverage

**Tier A** bridge.

| UMP MT | Direction | Spec | Notes |
|---|---|---|---|
| 0x0 Utility | both | M2-104-UM §3 | JR Timestamp passthrough |
| 0x2 MIDI 1.0 Channel Voice in UMP | upstream→PC | M2-104-UM §6 | uplifted from `alt=0` USB-MIDI 1.0 cable events |
| 0x4 MIDI 2.0 Channel Voice | both | M2-104-UM §7 | NoteOn/Off, CC, Pitch Bend, Per-Note family, all forwarded |
| 0xF UMP Stream | both | M2-104-UM §11 | Endpoint Discovery answered locally on each side, not proxied |

MIDI-CI is not bridged: each USB link runs its own Initiator / Responder when applicable.

## Showcase

Three modes, switching automatically based on connectivity.

**`Waiting`** (no PC mount yet): splash + spinner.

**`Showcase`** (PC mounted, no upstream on USB-A): bridge emits its own UMP from the device side so a connected DAW can validate the link without an upstream.

- Chromatic walk C4 to B4: NoteOn/Off every 250 ms (24 steps total, MT 0x4, group 0, ch 0, vel `0xC000`)
- CC #74 (Brightness) 32-bit sweep every 6 s (5 points across the 32-bit range)

**`Bridging`** (PC mounted, upstream on USB-A): showcase pauses, forward path takes over.

- Upstream UMP flows raw to the PC (group preserved, no remap)
- PC UMP flows to the upstream when the upstream is MIDI 2.0 (alt=1)
- USB-MIDI 1.0 upstream cable events uplifted to UMP MT 0x2

UART debug on GP0 mirrors mount events.

## v0.1 scope and limitations

- **Single upstream device** at a time (idx 0). A second device plugged in is enumerated by TinyUSB but not forwarded; OLED logs the mount, no traffic flows.
- **MIDI 1.0 uplift is one-way**: upstream cable events become UMP MT 0x2 on the PC. PC to upstream UMP is forwarded only when the upstream is MIDI 2.0; MIDI 1.0 alt=0 downstream UMP is dropped silently.
- **Group remap is 1:1**: whatever group the upstream emits is the group the PC sees.
- **No CI bridging**: each USB link runs its own MIDI-CI Initiator / Responder when applicable. CI traffic is not proxied across the bridge.

## Hot-swap caveat

A 3 s watchdog in `feather_bridge::task` resets the host side (`tuh_deinit` + `tusb_init`) after the upstream device has been gone for `MIDI2CPP_BRIDGE_WATCHDOG_MS`. Tune at compile time:

```bash
cmake -B build -DMIDI2CPP_BRIDGE_WATCHDOG_MS=5000   # 5 s
cmake -B build -DMIDI2CPP_BRIDGE_WATCHDOG_MS=0      # disable
```

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE). Pico-PIO-USB is MIT.

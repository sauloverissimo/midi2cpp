# [midi2cpp](../..) | Host MIDI 2.0
## LilyGo T-Display S3 + LilyGo MIDI Shield V1.1

USB MIDI 2.0 host on the **LilyGo T-Display S3** docked into the **LilyGo MIDI Shield V1.1**, with on-board ST7789 piano roll visualisation. Plug a USB MIDI 2.0 device into the Shield's USB-A jack; the T-Display lights piano keys in real time. Built on two **released** libraries: [`ESP32_Host_MIDI`](https://github.com/sauloverissimo/ESP32_Host_MIDI) v6.0.0 owns the wire on top of ESP-IDF's native USB host stack; `m2host` from `midi2cpp` owns the high level. PlatformIO build.

![t-display-s3-shield-host-midi2 banner](board/banner.png)

> ![official](https://img.shields.io/badge/-official-success.svg) **No fork, no override.** This recipe reaches MIDI 2.0 host capability without a single pending pull request in the stack.

## Topology

```
   USB MIDI 2.0 device
        |
        v   (USB-A jack on the Shield)
   USB-A jack (Shield)  -->  internal D+/D- shared bus  -->  USB-C IN (Shield)
                                                              |
                                                              v
   T-Display S3 USB-C  -->  ESP32-S3 USB-OTG (DWC2 FS, ESP-IDF native usb_host_*)
                                                              |
                                                              v
                                                          ESP32_Host_MIDI v6.0.0
                                                              |
                                                              v
                                                          midi2cpp m2host
                                                              |
                                                +-------------+-------------+
                                                |                           |
                                                v                           v
                                           Serial (UART0)             piano_display
                                       on GP43/GP44 header          on the on-board ST7789
```

The Shield's "Power" Type-C feeds the whole stack via 5 V regulator (RT9080). The Shield's USB-A and USB-C IN share the same D+/D- pair on the schematic; only one of the two is active at a time. This recipe operates in **host mode**, so the USB-A is the active port.

## USB identity

Host-only role: no USB VID / PID consumed. The host plays MIDI-CI **Initiator**: it transmits Discovery Inquiry on every device mount.

| Field | Value |
|---|---|
| Role | USB MIDI 2.0 Host |
| USB transport | ESP32-S3 USB-OTG, FS 12 Mbps, ESP-IDF native `usb_host_*` |
| Host MUID (CI Initiator) | seeded at boot from `esp_random()`, masked to 28 bits |
| MIDI-CI Manufacturer ID | `{0x7D, 0x00, 0x00}` |

## Build

Requires PlatformIO Core 6.x+, a LilyGo T-Display S3 (8 MB Octal PSRAM, ESP32-S3R8 silicon) seated in a LilyGo MIDI Shield V1.1, and a USB-TTL adapter wired to UART0 on the Shield's side header for the boot log.

```bash
cd pio
pio run
pio run -t upload
```

Consumes the parent `midi2cpp` library via `lib_extra_dirs = ../../..`. PSRAM Octal at 80 MHz is mandatory in `platformio.ini` (`board_build.psram_type = opi` + `-DBOARD_HAS_PSRAM`); the full-screen 320x170 16-bpp sprite is ~108 KB and lives in PSRAM.

### Flash, USB-Serial-JTAG quirk

The T-Display S3 has a **single USB-C port** wired to the ESP32-S3 USB-OTG. Before the firmware runs, the chip exposes the USB-Serial-JTAG ROM bootloader as `/dev/ttyACM0`; once the recipe boots, the same port becomes the USB host channel.

USB-Serial-JTAG has fake DTR / RTS, so `pio run -t upload` alone leaves the chip parked in ROM. Force a watchdog reset:

```bash
pio run -t upload --upload-port /dev/ttyACM0
python -m esptool --chip esp32s3 -p /dev/ttyACM0 --after watchdog_reset run
```

If the chip refuses download mode, hold **GP0 (BOOT)** while plugging in.

### Console wiring

The Shield does not bring a USB-to-UART bridge, and the single USB-C is owned by the host stack at runtime. Wire an external USB-TTL adapter to UART0 on the Shield's side header:

| Adapter pin | Shield silkscreen |
|---|---|
| RX | `TX` (GP43) |
| TX | `RX` (GP44) |
| GND | `GND` |

Default `115200 8N1`.

## Hardware

| Pin / signal | Use |
|---|---|
| USB-C (T-Display) -> USB-C IN (Shield) | T-Display USB-OTG hosts the Shield's D+/D- bus |
| USB-A (Shield) | USB Host A-side. Plug a USB MIDI 2.0 device here |
| Power Type-C (Shield) | 5 V supply for the whole stack |
| UART0 TX (GP43) | Console output @ 115200 8N1 (external USB-TTL adapter) |
| GP15 | LCD VDD power-enable. Driven HIGH at `piano_display::init()` before LovyanGFX panel init |
| GP6 / GP7 / GP5 | LCD CS / RS-DC / RST |
| GP8 / GP9 | LCD WR / RD strobes (parallel-8-bit bus) |
| GP39..42, GP45..48 | LCD D0..D7 parallel data lines |
| GP38 | LCD backlight (PWM, channel 7, 22 kHz) |
| GP0 (BOOT) | Hold during reset/plug-in to enter download mode |

The Shield's other modules (PCM5102 DAC, 2× MPR121 capacitive touch, microSD, PCA9535 I/O extender, QWIIC) are not used here; they remain available for future variants. Schematic at [`../t-display-s3-midi2/board/SCH_T-Display-S3-MIDI_V1.1.pdf`](../t-display-s3-midi2/board/SCH_T-Display-S3-MIDI_V1.1.pdf) (canonical copy in the sibling device recipe).

## Validation

Plug any USB MIDI 2.0 device into the Shield's USB-A. The UART log should print:

- `[Connected] dev=0` followed by an `[Identity]` block carrying the device's Endpoint Name, Product Instance ID, FB count, and CI MUID.
- One `[NoteOn]` / `[NoteOff]` / `[CC]` / `[PitchBnd]` / ... line per UMP packet.
- 32-bit values where the spec gives 32 bits, 16-bit velocity for Note On.

The on-board piano roll lights keys as the device emits Note On / Off (cyan for white keys, warm orange for black keys). Out-of-view active notes show as a red triangle (below) or blue triangle (above) on the info bar edges; auto-shift centres the active region after the first hit outside the current 25-key window.

## Spec coverage

Full UMP host showcase with visible piano UI on the on-board ST7789.

| UMP MT | Direction | Spec | Display reaction |
|---|---|---|---|
| 0x0 Utility (JR Timestamp) | RX | M2-104-UM §3 | counter, no UI footprint |
| 0x2 MIDI 1.0 Channel Voice in UMP | RX | M2-104-UM §6 | piano key lit / unlit |
| 0x3 SysEx7 | RX | M2-104-UM §8 | counter (Other) |
| 0x4 MIDI 2.0 Channel Voice | RX + TX | M2-104-UM §7 | piano key lit / unlit (16-bit velocity preserved); NoteOn/Off, CC (32-bit), Pitch Bend (32-bit), Channel Pressure, Poly Pressure, Program + Bank |
| 0x5 SysEx8 | RX | M2-104-UM §9 | counter (Other) |
| 0xD Flex Data | RX | M2-104-UM §10 | counter (Other); Tempo decoded |
| 0xF UMP Stream | RX | M2-104-UM §11 | discovered Endpoint Name shown on the info bar |

MIDI-CI: Discovery Initiator only (auto-fires on mount). Replies populate `DeviceIdentity::ciMuid` and `ciDiscovered`.

## Showcase

Always on:

- USB Host stack on OTG controller, FreeRTOS USB task on core 0.
- `m2host` Initiator MUID generated via `esp_random()` and printed at boot.
- Cross-core UMP queue (USB task -> main loop) for thread-safe delivery.
- `piano_display` render task on core 1 at ~60 fps; info bar shows `TDisplayS3   MIDI 2.0 Host   <status>` with live counters.

Per device mount:

| Phase | What happens | Where it shows up |
|---|---|---|
| Enumeration | Configuration descriptor walked, Alt 1 (`bcdMSC=0x0200`) claimed | (silent) |
| Endpoint Discovery | Endpoint Info, Endpoint Name, Product Instance ID, Stream Config Notify, per-FB Discovery Reply | `[Identity]` block on UART, Endpoint Name on the info bar |
| CI Discovery Inquiry | `m2host::notifyDeviceMounted` triggers `sendDiscoveryInquiry`; reply populates `ciMuid` | `[Identity]` block updated |
| UMP traffic | Every incoming UMP word decoded into typed callbacks | event line on UART, piano key lit / unlit on the display |

Per device unmount: `[Disconnected]` line, info bar resets to `waiting for device...`, stuck active notes are cleared.

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE). [`ESP32_Host_MIDI`](https://github.com/sauloverissimo/ESP32_Host_MIDI) is MIT. [`LovyanGFX`](https://github.com/lovyan03/LovyanGFX) is FreeBSD-style permissive. The LilyGo T-Display-S3 + MIDI Shield V1.1 schematic is published by LilyGO under GPL 3.0.

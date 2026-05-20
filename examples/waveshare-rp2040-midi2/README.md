# [midi2cpp](../..) | Device MIDI 2.0
## Waveshare RP2040 Pi Zero

Full-spec USB MIDI 2.0 device on the **Waveshare RP2040 Pi Zero** (compact 18.0 x 23.5 mm RP2040 board with USB-C). Headless single-file showcase of every MIDI 2.0 message category beyond MIDI 1.0. Pico SDK build, no Arduino IDE.

![waveshare-rp2040-midi2 banner](board/banner.png)

## USB identity

| Field | Value |
|---|---|
| VID:PID | `cafe:4072` (development-only) |
| Product | `RP2040PiZero` |
| Manufacturer | `github.com/sauloverissimo` |

## Build

Requires Pico SDK 2.x (with `PICO_SDK_PATH` exported), `arm-none-eabi-gcc`, CMake 3.14+.

```bash
cmake -B build         # first run fetches TinyUSB
cmake --build build -j
```

Pointing at a local TinyUSB checkout: `cmake -B build -DPICO_TINYUSB_PATH=/path/to/tinyusb`.

## Flash

Hold BOOT, plug USB-C, drag `build/waveshare-rp2040-midi2-showcase.uf2` to the mounted `RPI-RP2` drive.

## Hardware

| Pin | Use |
|---|---|
| USB-C | MIDI 2.0 device (only USB function) |
| GP0 / GP1 | UART TX/RX debug print @ 115200 8N1 |
| GP16 | On-board WS2812 RGB LED (not driven) |
| BOOT | Hold while plugging USB-C to enter BOOTSEL |

No on-board reset button on the standard model; power-cycle to reset.

## Validation

```bash
lsusb | grep cafe:4072
amidi -l
```

Plug straight into a laptop running Microsoft MIDI Services Console for end-to-end MIDI 2.0 enumeration:

![bench setup](monitor/stack.png)
![Microsoft MIDI Services Console](monitor/windows.png)

## Spec coverage

**Tier A** (full spec). The RP2040's 264 KB SRAM affords the complete UMP + MIDI-CI surface.

| UMP MT | Spec | Notes |
|---|---|---|
| 0x0 Utility | M2-104-UM §3 | JR heartbeat 500 ms, Delta Clockstamp |
| 0x4 MIDI 2.0 Channel Voice | M2-104-UM §7 | 32-bit CCs, Per-Note family, Note Attribute, RPN/NRPN, Relative RPN/NRPN |
| 0x3 SysEx7 | M2-104-UM §7.7 | up to 6 bytes per packet, auto-fragmented |
| 0xD Flex Data | M2-104-UM §10 | Tempo, Time Sig, Key Sig, Metronome, Chord Name, Start/End of Clip |
| 0xF UMP Stream | M2-104-UM §11 | full Endpoint + FB Discovery |

MIDI-CI: Discovery + Profiles (1 custom registered) + Property Exchange (3 properties: static, dynamic, subscribable) + Process Inquiry, all via the `m2ci` Appendix E convenience responder.

## Showcase

Always on while mounted: JR heartbeat (500 ms), UMP Stream + MIDI-CI Discovery responders, 1 custom Profile, 3 PE properties, Process Inquiry replies.

Per cycle (~22 s):

| Scene | Content | MIDI 2.0 only because |
|---|---|---|
| **A.** Flex Data | Tempo (120 BPM), Time Sig (4/4), Key Sig (C), Metronome, Chord Name (Cmaj7), Start of Clip | MT 0xD + 0xF |
| **B.** Per-Note | Sustained C4 with Per-Note Pitch Bend (5 Hz vibrato), Registered Per-Note Controller #7, Assignable Per-Note Controller #74, Per-Note Management Reset | Per-Note family is MIDI 2.0 only |
| **C.** Resolution | Chromatic walk C5→G#5 with 16-bit velocity ramp, 32-bit CC #74 sweep, 32-bit Pitch Bend, 32-bit Poly Pressure, 32-bit Channel Pressure | MIDI 1.0 caps at 7/14-bit |
| **D.** Program + Bank | Program Change with bank MSB/LSB in a single UMP | MIDI 1.0 needs three messages |
| **E.** RPN/NRPN | RPN 0/0, NRPN, Relative RPN (+delta), Relative NRPN (-delta) | RPN/NRPN first-class + Relative |
| **F.** Note Attribute | Note On with `attribute_type=0x03` (pitch_7_9), E4 +50 cents | Microtonal attribute |
| **G.** SysEx7 | Universal SysEx Identity Reply, 12 bytes, auto-fragmented (Start + End) | MT 0x3 |
| **H.** Delta Clockstamp | DCTPQ=480 + Delta Clockstamp=240 ticks | MT 0x0 utility |
| **I.** PE Notify | Broadcast `OverlayRate` change to subscribers (value increments per cycle) | Property Exchange |
| **J.** End of Clip | Sequencer End of Clip marker | MT 0xF status 0x21 |

Every scene logs to UART (GP0). Windows MIDI Services Console captures live in [`monitor/`](monitor/).

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE).

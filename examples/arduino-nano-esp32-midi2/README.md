# [midi2cpp](../..) | Device MIDI 2.0
## Arduino Nano ESP32

![arduino-esp32](board/banner.png)

Full-spec USB MIDI 2.0 device on the [**Arduino Nano ESP32**](https://docs.arduino.cc/hardware/nano-esp32/) (ESP32-S3-MINI-1 in Nano form factor, single-channel `LED_BUILTIN` on D13 / GPIO48). Headless single-file showcase of every MIDI 2.0 message category beyond MIDI 1.0. ESP-IDF v5.4 build, no Arduino IDE.

## USB identity

| Field | Value |
|---|---|
| VID:PID | `cafe:4093` (development-only) |
| Product | `ArduinoNanoESP32` |
| Manufacturer | `github.com/sauloverissimo` |

## Build

Requires ESP-IDF v5.4+ with `. $IDF_PATH/export.sh` sourced, an Arduino Nano ESP32 (ABX00083), USB-C cable.

```bash
cd idf
./scripts/fetch_tinyusb.sh         # one-off, ~36 MB clone of TinyUSB upstream
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The Nano exposes a single USB-C jack as native USB-OTG. Before firmware claims it, the chip exposes the USB-Serial-JTAG ROM bootloader as `/dev/ttyACM0`. After flashing, the firmware reclaims it as `cafe:4093`.

If `idf.py flash` does not auto-reset:

```bash
python -m esptool --chip esp32s3 -p /dev/ttyACM0 --after watchdog_reset run
```

## Hardware

| Pin | Use |
|---|---|
| USB-C | Native USB-OTG, MIDI 2.0 device (and ROM bootloader before firmware claims it) |
| D13 / GPIO48 | `LED_BUILTIN` (yellow). Lit while USB is mounted. Override with `-DLED_BUILTIN_GPIO=<n>` |
| D14 / GPIO46, D15 / GPIO0, D16 / GPIO45 | RGB LED (active LOW, not driven) |
| B1 (BOOT) | Hold during reset to enter the ESP32-S3 ROM bootloader |
| RESET | Reboot. Double-tap to enter the Arduino DFU bootloader |


![arduino-esp32](board/banner3.png)
## Validation

```bash
lsusb | grep cafe:4093
amidi -l
PORT=$(aseqdump -l | grep -i ArduinoNanoESP32 | awk '{print $1}' | tr -d ':')
timeout 30 aseqdump -p ${PORT}
```

## Spec coverage

Full spec. The ESP32-S3's 512 KB SRAM (plus 8 MB PSRAM) affords the complete UMP + MIDI-CI surface.

| UMP MT | Spec | Notes |
|---|---|---|
| 0x0 Utility | M2-104-UM §3 | JR heartbeat 500 ms, Delta Clockstamp |
| 0x4 MIDI 2.0 Channel Voice | M2-104-UM §7 | 32-bit CCs, Per-Note family, Note Attribute, RPN/NRPN, Relative RPN/NRPN |
| 0x3 SysEx7 | M2-104-UM §7.7 | up to 6 bytes per packet, auto-fragmented |
| 0xD Flex Data | M2-104-UM §10 | Tempo, Time Sig, Key Sig, Metronome, Chord Name, Start/End of Clip |
| 0xF UMP Stream | M2-104-UM §11 | full Endpoint + FB Discovery |

MIDI-CI: Discovery + Profiles (1 custom registered) + Property Exchange (3 properties: static, dynamic, subscribable) + Process Inquiry, all via the `m2ci` Appendix E convenience responder.

## Showcase
![stack](monitor/stack.jpg)

Always on while mounted: JR heartbeat (500 ms), UMP Stream + MIDI-CI Discovery responders, 1 custom Profile, 3 PE properties, Process Inquiry replies. D13 / GPIO48 LED lit.

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

Every scene logs via the ESP-IDF console (default UART monitor).

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE).

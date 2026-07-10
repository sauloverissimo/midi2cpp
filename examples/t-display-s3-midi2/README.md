# [midi2cpp](../..) | Device MIDI 2.0
## LilyGo T-Display S3 (receiver, on-board piano roll)

USB MIDI 2.0 device receiver on the **LilyGo T-Display S3** (ESP32-S3R8, 8 MB Octal PSRAM, 16 MB flash, ST7789 1.9" 320x170 IPS parallel 8-bit). Headless on the audio side, visual on the display side: the host sends UMP, the on-board piano roll mirrors note activity in real time. ESP-IDF v5.4 build, no Arduino IDE.

![t-display-s3-midi2 banner](board/banner.jpg)

This recipe is a **receiver showcase**: it does not emit notes, does not generate sound, does not synthesise audio. Its job is to be a well-formed USB MIDI 2.0 device, accept whatever a host (DAW, OS) sends over UMP, and visualise the activity on the on-board ST7789. The `piano_display` ESP-IDF component renders a 25-key roll plus an info bar (identity, USB lifecycle, per-category UMP counters) at ~60 fps from a dedicated FreeRTOS task pinned to core 1; the TinyUSB device task runs on core 0.

## USB identity

| Field | Value |
|---|---|
| VID:PID | `cafe:4094` (development-only) |
| Product | `LILYGO T-Display S3 MIDI 2.0` |
| Manufacturer | `midi2.diy` |

## Build

Requires ESP-IDF v5.4+ with `. $IDF_PATH/export.sh` sourced, a LilyGo T-Display S3 (8 MB Octal PSRAM, ESP32-S3R8 silicon), USB-C cable.

```bash
cd idf
./scripts/fetch_tinyusb.sh         # one-off, clones TinyUSB upstream (PR #3738, merged)
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash
```

PSRAM Octal at 80 MHz is mandatory in `sdkconfig.defaults`; the full-screen 320x170 16-bpp sprite is ~108 KB and lives in PSRAM. The R8N16 variant with Quad PSRAM does not work without flipping `CONFIG_SPIRAM_MODE_QUAD=y`.

LovyanGFX 1.2.0 is fetched from GitHub via the ESP-IDF Component Manager.

### Flash, USB-Serial-JTAG quirk

The T-Display S3 has a **single USB-C port** wired to the ESP32-S3 USB-OTG. Before the firmware runs, the chip exposes the USB-Serial-JTAG ROM bootloader as `/dev/ttyACM0`; once the recipe boots, the same port becomes the USB MIDI 2.0 device.

`Hard resetting via RTS pin...` is fake on USB-Serial-JTAG (no real DTR / RTS), so `idf.py flash` alone leaves the chip parked in ROM. Force a watchdog reset:

```bash
idf.py -p /dev/ttyACM0 flash
python -m esptool --chip esp32s3 -p /dev/ttyACM0 --after watchdog_reset run
```

If the chip refuses download mode, hold **GP0 (BOOT)** while plugging in.

### Console wiring

The T-Display S3 has no on-board USB-to-UART bridge and the single USB-C is owned by TinyUSB at runtime. Wire an external USB-TTL adapter to UART0 on the side header (GP43 TX / GP44 RX, GND, 115200 8N1).

To override TinyUSB with a local working copy: `ln -sfn /path/to/your/tinyusb idf/external/tinyusb && idf.py reconfigure`.

## Hardware

| Pin / signal | Use |
|---|---|
| USB-C | Native USB-OTG, MIDI 2.0 device interface (also flash path via USB-Serial-JTAG ROM before the app runs) |
| UART0 TX / RX (GP43 / GP44) | Console stdio @ 115200 8N1 (external USB-TTL adapter) |
| GP15 | LCD VDD power-enable. Driven HIGH at `piano_display::init()` before LovyanGFX panel init |
| GP6 / GP7 / GP5 | LCD CS / RS-DC / RST |
| GP8 / GP9 | LCD WR / RD strobes (parallel-8-bit bus) |
| GP39..42, GP45..48 | LCD D0..D7 parallel data lines |
| GP38 | LCD backlight (PWM, channel 7, 22 kHz) |
| GP0 (BOOT) | Hold during reset/plug-in to enter download mode. Also reachable from firmware as a user button |
| GP14 (KEY1) | User button, reserved for future controller variants |

## Validation

```bash
lsusb | grep cafe:4094
amidi -l                        # IO  hw:N,1,0  Group 1 (Main)
```

Drive notes from any MIDI 2.0 host and watch the on-board piano roll mirror them. Microsoft MIDI Services Console (Windows) shows `LILYGO T-Display S3 MIDI 2.0` with Native data format = UMP, MIDI 2.0 Protocol = True. Audio MIDI Setup (macOS) shows `LILYGO T-Display S3 MIDI 2.0` with one source / one destination.

`amidi -p hw:N,1,0 -S "903C7F"` on Linux sends a MIDI 1.0 NoteOn (C4 at velocity `7F`) into the MIDI 2.0 alt setting; ALSA upscales the byte stream into UMP MT 0x2 and the receiver lights the centre key of the piano roll.

![bench setup](monitor/stack.png)
![Microsoft MIDI Services Console driving the receiver](monitor/windows.png)

## Spec coverage

Full UMP receiver. The recipe decodes the full inbound UMP surface and responds to host Discovery; the Showcase section describes how each decoded category lands on the display.

| UMP MT | Direction | Spec | Showcase reaction |
|---|---|---|---|
| 0x0 Utility | RX + TX (heartbeat) | M2-104-UM §3 | counter, no UI footprint |
| 0x2 MIDI 1.0 Channel Voice | RX | M2-104-UM §6 | piano key lit / unlit; counter |
| 0x3 SysEx7 | RX | M2-104-UM §8 | counter (Other) |
| 0x4 MIDI 2.0 Channel Voice | RX | M2-104-UM §7 | piano key lit / unlit (16-bit velocity preserved); counter |
| 0x5 SysEx8 | RX | M2-104-UM §9 | counter (Other) |
| 0xD Flex Data | RX | M2-104-UM §10 | counter (Other) |
| 0xF UMP Stream | TX (responder) | M2-104-UM §11 | (no UI) |

MIDI-CI: Discovery + Profiles (GM 1, `7E 00 00 01 00`) + Property Exchange (Capability + Get on static `DeviceInfo`) + Process Inquiry, all via the `m2ci` Appendix E convenience responder.

## Showcase

The recipe is reactive: it has no scene timeline of its own, it visualises what the host pushes.

Always on while mounted:

- JR Timestamp heartbeat every 500 ms (MT 0x0 status 0x2)
- UMP Stream Discovery responder (MT 0xF), full Endpoint + FB Discovery surface
- MIDI-CI Discovery + PE Capability + PE Get auto-replied
- Piano render task at ~60 fps on core 1, redraws the 25-key roll + info bar from the active-note buffer + counter atomics

On host activity:

| Event | Display reaction |
|---|---|
| MIDI 2.0 NoteOn (MT 0x4) | piano key lit (cyan if white, warm orange if black); `On` counter increments |
| MIDI 2.0 NoteOff (MT 0x4) | piano key unlit; `Off` counter increments |
| MIDI 1.0 NoteOn / NoteOff (MT 0x2) | same, both event types feed the same active-note buffer |
| Note out of view | piano auto-shifts so the active region centres on the new note; out-of-view active notes show as a red triangle (below) or blue triangle (above) on the info bar edges |
| CC | `CC` counter increments, no per-key effect |
| Pitch Bend | `PB` counter increments |
| Anything else (PE, SysEx, Flex Data) | `Other` counter increments |
| Host disconnect | status banner flips to `waiting for host...`; active notes stay lit until a NoteOff arrives or the user reboots |

Info bar reads `TDisplayS3   MIDI 2.0 RX   <status>` on the top row, then `On <n>  Off <n>  CC <n>  PB <n>  Other <n>` underneath, plus a `<low note>-<high note>` range label and the out-of-view triangles.

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE). LovyanGFX is FreeBSD-style permissive.

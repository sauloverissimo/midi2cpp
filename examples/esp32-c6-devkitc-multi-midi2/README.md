# [midi2cpp](../..) | Device MIDI 2.0
## ESP32-C6-DevKitC-1 (BLE + ESP-NOW)

Wireless MIDI 2.0 device on the **ESP32-C6-DevKitC-1**, exposing two transports in parallel: **BLE-MIDI 1.0** (standard Apple / MIDI Association service UUID) and **ESP-NOW** (peer-to-peer, broadcast on WiFi channel 1). The C6 has no USB-OTG hardware (only USB-Serial-JTAG), so the canonical USB MIDI 2.0 device interface is unavailable on this chip; this recipe demonstrates the wireless path instead. Built on [`ESP32_Host_MIDI`](https://github.com/sauloverissimo/ESP32_Host_MIDI) v6.0.0 + arduino-esp32 v3.x. PlatformIO build.

![esp32-c6-devkitc-multi-midi2 banner](board/banner.png)

> ![official](https://img.shields.io/badge/-official-success.svg) **No fork, no override.** This recipe uses no TinyUSB and depends on no pending pull request.

Both wire transports carry MIDI 1.0 byte streams natively (BLE-MIDI 1.0 spec, ESP-NOW small payload). Bytes are uplifted in firmware to UMP MT 0x2 via `midi2::ByteStreamConverter` so the application sees the same typed `midi2::Device` dispatch surface used by the USB recipes; outbound UMP from the showcase loop is downgraded to MIDI 1.0 bytes before hitting the wire.

## Identity

No USB device interface, no PID consumed. Identity is per transport.

### BLE

| Field | Value |
|---|---|
| Role | BLE peripheral (GATT server) |
| Service UUID | `03B80E5A-EDE8-4B33-A751-6CE34EC4C700` (BLE-MIDI 1.0, Apple / MIDI Association) |
| Characteristic UUID | `7772E5DB-3868-4112-A1A9-F2669D106BF3` |
| Advertised name | `Esp32C6Multi` |
| MAC | burned-in (read at boot, printed to UART) |

### ESP-NOW

| Field | Value |
|---|---|
| Role | ESP-NOW broadcaster + receiver, WiFi STA mode (no AP association) |
| Channel | 1 (recipe constant, both peers must match) |
| Peer addressing | broadcast on `FF:FF:FF:FF:FF:FF` by default; `ESPNowConnection::addPeer` switches to unicast |
| Local MAC | burned-in (printed at boot for pairing) |

## Build

Requires PlatformIO Core 6.x+, an ESP32-C6-DevKitC-1, USB-C cable to the right-side jack (CH340 USB-to-UART for log + flash).

```bash
cd pio
pio run
pio run -t upload -t monitor
```

The C6 is not in the upstream `espressif32` PlatformIO platform; support comes from [`pioarduino/platform-espressif32`](https://github.com/pioarduino/platform-espressif32) pinned to release `53.03.13` in `platformio.ini`. Partition table is `huge_app.csv` (3 MB app slot) because BLE + WiFi + ESP-NOW + arduino-esp32 v3 exceeds the default 1.6 MB layout.

### Flash via the left jack (USB-Serial-JTAG, recommended)

The C6 has a built-in USB-Serial-JTAG controller wired to the left jack (`303a:1001 Espressif USB JTAG/serial debug unit`). esptool talks to it directly over USB DFU, no auto-reset circuit involved.

```bash
pio run -t upload --upload-port /dev/ttyACM1
pio device monitor -p /dev/ttyACM0      # monitor on the right jack (CH340)
```

### Flash via the right jack (CH340)

The CH340 path depends on auto-reset via DTR / RTS. On some board revisions strapping jumpers J1 / J2 are not populated and esptool reports `Failed to connect: No serial data received`. Fall back to the USB-Serial-JTAG path above, or hold BOOT, pulse RST, release BOOT.

```bash
pio run -t upload -t monitor
```

## Hardware

| Pin / port | Use |
|---|---|
| USB-C (right jack, CH340) | Host UART log @ 115200 8N1 + esptool flash entry. Default `/dev/ttyACM0` |
| USB-C (left jack, USB-Serial-JTAG) | Native USB-Serial-JTAG for flashing + secondary console |
| 2.4 GHz PCB antenna | Shared by WiFi (ESP-NOW) and Bluetooth 5 LE (BLE-MIDI). Coexistence handled by the WiFi / BT controller |
| GPIO8 | On-board RGB LED (WS2812). Not driven |
| BOOT (GPIO9) | Hold during reset to enter download mode |

The C6 is single-core 32-bit RISC-V Wi-Fi 6 + Bluetooth 5 LE + 802.15.4. Only the WiFi 4 / 5 paths in arduino-esp32 v3 are exercised here. 802.15.4 is unused.

## Validation

```bash
pio device monitor -p /dev/ttyACM0
```

After flash the boot banner prints, followed by `[BLE] advertised name = "Esp32C6Multi"` and `[ESPNW] begin() = ok, channel = 1, local MAC = XX:XX:XX:XX:XX:XX`.

**BLE**: pair `Esp32C6Multi` from any BLE-MIDI 1.0 client (iOS / iPadOS Audio MIDI Setup, macOS Audio MIDI Setup with the BLE bridge, Android USB MIDI BLE Bridge, MIDI BLE Connect on Windows, BlueZ 5.65+ on Linux). The C major scale appears as Note On / Off in the host's MIDI monitor.

**ESP-NOW**: flash a second ESP32 on channel 1 in receive mode (any `esp-now` responder example or another copy of this firmware). Notes emitted by board A print on the UART of board B.

Sending notes back from either transport prints `[NoteOn  ] BLE   ...` or `[NoteOn  ] ESPNW ...` on this board's UART.

## Spec coverage

**Tier B** (MIDI 1.0 wire, UMP MT 0x2 surface). The C6 is RAM-rich (512 KB SRAM) but the wire transports cap at MIDI 1.0 byte payloads.

| UMP MT | Direction | Spec | Notes |
|---|---|---|---|
| 0x2 MIDI 1.0 Channel Voice | RX + TX | M2-104-UM §6 | uplifted from BLE / ESP-NOW byte streams via `midi2::ByteStreamConverter`, dispatched through `Device::onNoteOn / onNoteOff / onCC / onProgram / onPitchBend / onChannelPressure / onPolyPressure` |
| 0x4 MIDI 2.0 Channel Voice | TX only | M2-104-UM §7 | the `setWriteFn` fan-out detects MT 0x4 and downgrades to MT 0x2 via `Device::downgradeMt4ToMt2` before serialising. RX not exercised, neither wire carries 32-bit values |
| 0x1 System Real-Time / Common | TX | M2-104-UM §4 | one byte per System UMP word; not exercised by the showcase |

SysEx7 (multi-packet across BLE 20-byte / ESP-NOW 4-byte frames), SysEx8, Flex Data, UMP Stream are dropped. Reassembly across packets requires per-transport state not in v0.1 scope. Promoting this recipe to Tier A would require a UMP-over-BLE custom service or UMP-over-ESP-NOW packing scheme, neither in scope.

MIDI-CI: not applicable (no MIDI-CI traffic on the wire because both transports are MIDI 1.0 byte streams, and SysEx-based MIDI-CI requires the missing reassembly path).

## Showcase

Always on:

- BLE advertiser running with name `Esp32C6Multi`
- ESP-NOW STA mode active on channel 1, ready to broadcast / receive on `FF:FF:FF:FF:FF:FF`
- `midi2::Device` wired with typed callbacks; `ByteStreamConverter` active per transport (group 0 = BLE, group 1 = ESP-NOW)

Showcase loop every 350 ms:

| Step | Bytes emitted | Where it goes |
|---|---|---|
| Note Off (previous note) | `0x80 / note / 0x00` | BLE notify + ESP-NOW broadcast |
| Note On (next scale degree) | `0x90 / note / 0x60` | BLE notify + ESP-NOW broadcast |
| CC #1 sweep | `0xB0 / 0x01 / value` | BLE notify + ESP-NOW broadcast |

Scale walked is C major (`60, 62, 64, 65, 67, 69, 71, 72`); `value` for CC #1 increments by 16 per step and wraps after the octave.

Per inbound packet (any MIDI 1.0 byte stream on either transport, uplifted to UMP MT 0x2):

```
[NoteOn  ] BLE   g=0 ch=1  note=60  vel=0x6000
[NoteOff ] BLE   g=0 ch=1  note=60  vel=0x0000
[CC #1   ] ESPNW g=1 ch=1  val=0xFFFFFFFF
```

Velocity / value widths reflect the 7-bit to 16-bit / 32-bit upscale done by the dispatch layer; the wire side stays MIDI 1.0.

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE). [`ESP32_Host_MIDI`](https://github.com/sauloverissimo/ESP32_Host_MIDI) is also MIT. Board reference images and PDFs under `board/` are © Espressif Systems, redistributed for documentation purposes.

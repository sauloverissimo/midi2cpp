# [midi2cpp](../..) | Host MIDI 2.0
## Waveshare ESP32-P4-WIFI6-DEV-KIT

USB MIDI 2.0 host on the **Waveshare ESP32-P4-WIFI6-DEV-KIT**. Plug an upstream MIDI 2.0 device into either of the two USB-A jacks (UTMI PHY, OTG_HS controller at 480 Mbps, routed through the onboard CH334F USB hub), routes UMP through `m2host`, prints decoded device topology + live UMP stream on the UART console (CH343 USB-Serial-JTAG bridge on the **ToUART** USB-C jack). ESP-IDF v5.4 build, no Arduino IDE.

![esp32-p4-devkit-host-midi2 banner](board/banner.jpg)

> Built against the TinyUSB [`experiment/midi-coexistence`](https://github.com/sauloverissimo/tinyusb/tree/experiment/midi-coexistence) branch on top of upstream master. The branch adds an alt-walk `bcdMSC` defer that lets `CFG_TUH_MIDI=1` and `CFG_TUH_MIDI2=1` coexist (each driver claims only its matching protocol version). Staged as a follow-up PR upstream.

## USB identity

Host-only role: no USB VID / PID consumed. The host plays MIDI-CI **Initiator**: it transmits Discovery Inquiry on every device mount and stores remote MUIDs in `m2host::identity(idx).ciMuid`.

| Field | Value |
|---|---|
| MIDI-CI Manufacturer ID | `{0x7D, 0x00, 0x00}` |
| Host MUID | seeded at boot from `esp_random()`, masked to 28 bits |

## Build

Requires ESP-IDF v5.4+ with `. $IDF_PATH/export.sh` sourced and the RISC-V toolchain installed (`$IDF_PATH/install.sh esp32p4` once on a fresh IDF).

```bash
cd idf
./scripts/fetch_tinyusb.sh         # one-off, ~36 MB clone of the experiment branch
. $IDF_PATH/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor    # ToUART jack
```

The CH343 USB-Serial-JTAG bridge (VID `1a86:55d3`) on the **ToUART** USB-C jack binds to `cdc_acm` (`/dev/ttyACM0`). Real DTR / RTS, so `idf.py flash` auto-resets without a button press.

To override TinyUSB with a local working copy: `ln -sfn /path/to/your/tinyusb idf/external/tinyusb && idf.py reconfigure`.

## Hardware
![esp32-p4-devkit-host-midi2 banner](board/hardware.png)

| Connector / Pin | Use |
|---|---|
| USB-A jacks (×2) | UTMI host PHY (OTG_HS, 480 Mbps), routed through onboard CH334F USB hub. Plug upstream MIDI 2.0 devices here. |
| USB-C **ToUART** | CH343 USB-Serial-JTAG bridge, console stdio @ 115200 8N1, flashing |
| USB-C **USB-Device** | INT device PHY (OTG_FS), not used in this host-only recipe |
| BOOT button | Hold during reset to enter download mode (rarely needed; CH343 auto-reset handles it) |
| RESET button | Reboot |

The CH334F hub allows up to `MIDI2CPP_HOST_MAX_DEVICES` (default 4) MIDI 2.0 devices simultaneously, addressed by `idx`.

## Validation

Plug any USB MIDI 2.0 device into either USB-A jack. The UART console should print `[host] device idx=N connected, alt=A`, an `[ep]` block with the device's Endpoint Info / Name / Product Instance ID, and one event line per UMP packet.

## Spec coverage

Full UMP host. The ESP32-P4's 768 KB SRAM and high-speed UTMI PHY support multi-device hub topology end to end.

| UMP MT | Direction | Spec | Notes |
|---|---|---|---|
| 0x0 Utility | RX | M2-104-UM §3 | JR Timestamp tracked |
| 0x4 MIDI 2.0 Channel Voice | RX | M2-104-UM §7 | NoteOn/Off (16-bit velocity), CC (32-bit), Pitch Bend (32-bit), all surfaced |
| 0xF UMP Stream | RX | M2-104-UM §11 | full Endpoint + FB Discovery, Endpoint Name notification surfaced |

MIDI-CI: Discovery Initiator only (auto-fired on mount). Replies populate `m2host::identity(idx).ciMuid`.

## Showcase
![esp32-p4-devkit-host-midi2 banner](board/hardware.png)

Boot log:

```
[boot] esp32-p4-devkit-host-midi2-monitor
Host UTMI PHY ready (rhport 1)
[host] waiting for device on USB-A jacks...
```

Per device mount, console prints one line per event:

| Event | Console line |
|---|---|
| Mount | `[host] device idx=N connected, alt=A (UMP\|byte-stream)` |
| Endpoint Info notification | `[ep] idx=N UMP vM.m, F FB, MIDI2=1` |
| Endpoint Name notification | `[ep] idx=N Endpoint Name: <product>` |
| NoteOn | `[in idxN] NoteOn ch=C note=N vel=0xVVVV` |
| NoteOff | `[in idxN] NoteOff ch=C note=N vel=0xVVVV` |
| CC (32-bit) | `[in idxN] CC ch=C #I val=0xVVVVVVVV` |
| Pitch Bend (32-bit) | `[in idxN] PitchBend ch=C val=0xVVVVVVVV` |
| Disconnect | `[host] device idx=N disconnected` |

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE).

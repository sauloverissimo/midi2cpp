# [midi2cpp](../..) | Bridge MIDI 2.0
## Waveshare ESP32-P4-WIFI6-DEV-KIT (m2bridge variant)

Dual-stack USB MIDI 2.0 bridge: UTMI host PHY on the USB-A jacks, INT device PHY on the USB-Device USB-C jack, `midi2::m2bridge` in between. All bridge logic lives in [`src/midi2_bridge.cpp`](../../src/midi2_bridge.cpp); the recipe is platform glue. PID `0x4095`, distinct from the v1 sibling [`esp32-p4-devkit-bridge-midi2`](../esp32-p4-devkit-bridge-midi2/) (`0x4092`).

![esp32-p4-devkit-bridge2-midi2 banner](board/banner.png)

> Built against the TinyUSB [`experiment/midi-coexistence`](https://github.com/sauloverissimo/tinyusb/tree/experiment/midi-coexistence) branch (MIDI 1.0 + 2.0 host coexistence, `CFG_TUD_MIDI2_USER_RESPONDER`). Staged as follow-up PRs upstream.

## Topology

Three device slots of 4 groups each (groups 1-12) plus one Function Block owned by the bridge itself (groups 13-16), where its own MIDI-CI responder lives. Up to 3 upstream MIDI 1.0 / 2.0 devices via the onboard CH334F hub.

Slots are bound to device identity (complete Product Instance Id, else Endpoint Name), persisted in NVS: a board keeps its Function Block across power cycles (M2-104 7.1.8), and an absent board leaves its block inactive without moving the others. A device with no identity gets a free slot after 3 s, not persisted.

Boards are enumerated at boot; the host does not re-enumerate late arrivals. Power-cycle the bridge with the boards attached.

## USB identity

| Field | Value |
|---|---|
| VID:PID | `cafe:4095` (development-only) |
| Product | `ESP32-P4 Bridge2 MIDI 2.0` |
| Manufacturer | `midi2.diy` |
| MIDI 2.0 Groups | 16 (3 slots of 4 + the bridge's own FB on groups 13-16) |
| Function Blocks | 4: three identity-bound device slots + the bridge itself |

The bridge is a CI Responder towards the PC and a CI Initiator towards upstream devices, each side with its own MUID.

## Build

Requires ESP-IDF v5.4+ with the esp32p4 toolchain installed.

```bash
cd idf
./scripts/fetch_tinyusb.sh         # one-off, ~36 MB clone of the experiment branch
. $IDF_PATH/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor    # ToUART jack, CH343 auto-reset
```

## Hardware
![esp32-p4-devkit-bridge2-midi2 banner](board/hardware.png)

| Connector | Use |
|---|---|
| USB-C **USB-Device** | INT device PHY (OTG_FS), routed to the PC. Mandatory `LP_SYS.usb_ctrl` PHY swap applied at boot |
| USB-A jacks (×2) | UTMI host PHY (OTG_HS), through onboard CH334F hub. Upstream MIDI 1.0 / 2.0 devices |
| USB-C **ToUART** | CH343 bridge, console stdio @ 115200 8N1, flashing |

## Validation
![esp32-p4-devkit-bridge2-midi2 banner](monitor/stack.png)

The PC enumerates `cafe:4095` with 4 Function Blocks: each board on its bound block, the bridge itself on groups 13-16.

- **Linux**: `aconnect -l` lists the group windows with each board's Endpoint Name; `aseqdump` on a window shows that board's traffic.
- **Windows**: MIDI Services Console shows Native data format = UMP, MIDI 2.0 Protocol = True, Function Block Count = 4, names per block.

Any device recipe in this repo works as an upstream board, e.g. [`rp2040-midi2`](../rp2040-midi2/) or [`esp32-s3-devkitc-usb-midi2`](../esp32-s3-devkitc-usb-midi2/).

## Spec coverage

| UMP MT | Spec | Bridge behaviour |
|---|---|---|
| 0x0 Utility | M2-104-UM §7.2 | not forwarded; bridge owns its JR Timestamp heartbeat |
| 0x1 System Common / Real Time | M2-104-UM §7.6 | forwarded with group rewrite |
| 0x2 MIDI 1.0 Channel Voice | M2-104-UM §7.3 | forwarded with group rewrite (also the MIDI 1.0 alt 0 uplift destination) |
| 0x3 SysEx7 | M2-104-UM §7.7 | forwarded with group rewrite |
| 0x4 MIDI 2.0 Channel Voice | M2-104-UM §7.4 | forwarded with group rewrite |
| 0x5 SysEx8 / Mixed Data | M2-104-UM §7.8-7.9 | forwarded with group rewrite |
| 0xD Flex Data | M2-104-UM §7.5 | forwarded with group rewrite |
| 0xF UMP Stream | M2-104-UM §7.1 | not forwarded; bridge owns Endpoint + FB Discovery on both sides |

MIDI-CI: each slot window is exclusive, so PC MIDI-CI reaches the upstream device end to end; the bridge's own `m2ci` answers only on groups 13-16. Upstream Discovery is paced at 3 s per MIDI-CI 5.5.5.

Not covered: upstream Stream messages are not passed through (each side answers Discovery locally), and PC-to-upstream traffic reaches MIDI 1.0 alt 0 devices as UMP only (no byte-stream conversion).

## License

MIT, inherits the parent [`midi2cpp` LICENSE](../../LICENSE). TinyUSB is MIT.

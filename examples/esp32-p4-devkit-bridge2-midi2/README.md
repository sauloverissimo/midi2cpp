# [midi2cpp](../..) | Bridge MIDI 2.0
## Waveshare ESP32-P4-WIFI6-DEV-KIT (m2bridge variant)

Dual-stack USB MIDI 2.0 bridge on the **Waveshare ESP32-P4-WIFI6-DEV-KIT**, identical wire role to the v1 sibling at [`esp32-p4-devkit-bridge-midi2`](../esp32-p4-devkit-bridge-midi2/) but built on top of the reusable `midi2::m2bridge` class. The slot table, multi-FB Stream Discovery responder, per-FB group rewrite, dynamic FB Names, and MIDI 1.0 alt 0 byte uplift all live inside `midi2cpp/src/midi2_bridge.cpp`; the recipe carries only the platform layer (PHY init, TinyUSB tasks, write callbacks, mount-event forwarding into the bridge slot table). PC sees PID `0x4095` / `ESP32P4Bridge2`, distinct from the v1 sibling's `0x4092` / `ESP32P4Bridge`, so both firmwares can coexist on the same host for A/B comparison.

![esp32-p4-devkit-bridge2-midi2 banner](board/banner.png)

> Same TinyUSB pin as the v1 sibling: built against the TinyUSB [`experiment/midi-coexistence`](https://github.com/sauloverissimo/tinyusb/tree/experiment/midi-coexistence) branch on top of upstream master. The branch adds the alt-walk `bcdMSC` defer plus the `CFG_TUD_MIDI2_USER_RESPONDER` opt-in that lets the app own MT 0xF Stream messages instead of the built-in responder consuming them. Staged as follow-up PRs upstream.

## What this is

Same role as the v1 sibling. The recipe boots both USB PHYs on the P4 (UTMI host on the USB-A jacks, INT device on the USB-Device USB-C jack), spawns the TinyUSB host + device tasks, and forwards UMP between any upstream MIDI 1.0 / 2.0 device and the host PC. The Function Block topology, the multi-slot group window mapping, the dynamic FB Name updates pulled from each upstream Endpoint Name, and the MIDI 1.0 alt 0 byte-stream uplift are all owned by `midi2::m2bridge`; the platform layer is around 240 lines of TinyUSB glue.

After `esp32_p4_devkit_bridge2::init(bridge)` returns, the application sees only `midi2::m2bridge`. It never touches `tud_*`, `tuh_*`, or any USB symbol.

## What this is not

This is reference firmware on top of the reusable bridge class, NOT a finished MIDI router product. Concrete consumer-facing applications (DAW-targeted multi-port aggregator, MIDI processor, MPE expander) are downstream of this recipe and live in their own projects.

## Topology

```
                                   ┌────────────────────────────────────────┐
PC / DAW ─── USB-Device USB-C ────►│ Waveshare ESP32-P4-WIFI6-DEV-KIT       │
              (INT PHY, OTG_FS,    │   rhport 0   m2device + m2ci           │
               LP_SYS swap)        │      ▲       (owned by m2bridge)       │
                                   │      │ raw UMP forward + group rewrite │
                                   │      ▼       (inside m2bridge)         │
                                   │   rhport 1   m2host (auto-discovery)   │
                                   └────────────────────────────────────────┘
                                          ▲
                                          │ USB-A (UTMI PHY, OTG_HS)
                                          │ via onboard CH334F hub
                                          │
                                  MIDI 2.0 + MIDI 1.0 devices
                                  up to 4 simultaneous via the hub
```

## Why a v2 recipe at all

The v1 sibling proved the bridge pattern in isolation: each piece (slot table, group window, FB Name dispatch, MIDI 1.0 uplift, USER_RESPONDER hook, per-packet `feedRx` iteration) was discovered and stabilised inside its `main.cpp` + `esp32_p4_devkit_bridge.cpp` pair. With the API now stable, the v2 recipe extracts that work into `midi2::m2bridge`, demonstrates the reusable shape end to end, and ships alongside v1 for an A/B comparison on the same hardware. Once `m2bridge` is validated across multiple boards, the v1 recipe can be retired or converted to a thin shim too.

## USB identity

What the PC sees on the device side (USB-Device USB-C jack):

| Field | Value |
|---|---|
| VID:PID | `cafe:4095` (development-only) |
| Product | `ESP32P4Bridge2` |
| Manufacturer | `github.com/sauloverissimo` |
| MIDI 2.0 Groups | 16 (4 per slot, 4 slots) |
| Function Blocks | 4, one per slot, name pulled from upstream Endpoint Name |

The bridge also runs an `m2host` instance (owned by `m2bridge`) for the upstream side, with its own MUID seeded from `esp_random()` masked to 28 bits, so the bridge is a CI Initiator towards upstream devices and a CI Responder towards the PC at the same time.

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

The CH343 USB-Serial-JTAG bridge on the **ToUART** USB-C jack binds to `/dev/ttyACM0` with real DTR / RTS, so `idf.py flash` auto-resets without a button press.

To override TinyUSB with a local working copy: `ln -sfn /path/to/your/tinyusb idf/external/tinyusb && idf.py reconfigure`.

## Hardware
![esp32-p4-devkit-bridge2-midi2 banner](board/hardware.png)

| Connector / Pin | Use |
|---|---|
| USB-C **USB-Device** | INT device PHY (OTG_FS), routed to the PC. Mandatory `LP_SYS.usb_ctrl` PHY swap applied at boot |
| USB-A jacks (×2) | UTMI host PHY (OTG_HS, 480 Mbps), through onboard CH334F hub. Plug upstream MIDI 1.0 / 2.0 devices here |
| USB-C **ToUART** | CH343 USB-Serial-JTAG bridge, console stdio @ 115200 8N1, flashing |
| BOOT button | Hold during reset to enter download mode (rarely needed; CH343 auto-reset handles it) |
| RESET button | Reboot |

Identical to the v1 sibling. No board modification required.

## Validation
![esp32-p4-devkit-bridge2-midi2 banner](monitor/stack.png)

Plug the **USB-Device** USB-C into the host PC. Plug any USB MIDI 2.0 device into either USB-A jack. The PC should enumerate `cafe:4095 ESP32P4Bridge2` and expose a multi-FB MIDI 2.0 endpoint.

- **Linux**: `lsusb | grep cafe:4095` shows `ESP32P4Bridge2`. `aconnect -l` lists the four group ranges with the upstream Endpoint Name in parentheses (e.g. `Group 1 (RP2040PiZero)`). `aseqdump -p "ESP32P4Bridge2:1"` and `aseqdump -p "ESP32P4Bridge2:5"` show two upstream devices on disjoint groups in real time.
- **Windows**: Microsoft MIDI Services Console shows `ESP32P4Bridge2` with Native data format = UMP, MIDI 2.0 Protocol = True, Declared Function Block Count = 4. Each active FB carries the upstream Endpoint Name; inactive FBs read `(empty slot)`.
- **macOS**: Audio MIDI Setup shows `ESP32P4Bridge2` with the four group windows visible.

The UART console mirrors the lifecycle:

```
[boot] esp32-p4-devkit-bridge2-midi2 (m2bridge)
Host UTMI PHY ready (rhport 1)
Device INT PHY ready, full speed (rhport 0)
Both TinyUSB tasks started (device on core 0, host on core 1)
[bridge] PC sees ESP32P4Bridge2 (cafe:4095), 16 groups across 4 FBs.

[tuh-midi2] descriptor idx=0 bcdMSC=02.00
[tuh-midi2] mount idx=0 proto=1 rxCables=1 alt=1
```

> Sibling cross-link: pair this recipe with [`esp32-p4-devkit-bridge-midi2`](../esp32-p4-devkit-bridge-midi2/) for an A/B comparison of the inline-glue (v1) and `m2bridge`-class (v2) implementations on the same hardware. Plug an upstream MIDI 2.0 device (any of [`rp2040-midi2`](../rp2040-midi2/), [`waveshare-rp2040-midi2`](../waveshare-rp2040-midi2/), [`arduino-nano-esp32-midi2`](../arduino-nano-esp32-midi2/), [`esp32-s3-devkitc-usb-midi2`](../esp32-s3-devkitc-usb-midi2/), [`esp32-p4-devkit-usb-midi2`](../esp32-p4-devkit-usb-midi2/)) into either USB-A jack and confirm that both bridges expose it identically.

The CH334F hub allows up to 4 MIDI 2.0 devices simultaneously (one direct + a 3-port external hub) plus legacy MIDI 1.0 controllers via the alt-walk `bcdMSC` defer.

## Spec coverage

Bridge. The bridge does not constrain the UMP surface: any message type emitted by an upstream device is forwarded to the PC verbatim except for MT 0x0 (utility / JR), MT 0xE (reserved) and MT 0xF (Stream), which are owned locally per the design contract.

| UMP MT | Spec | Bridge behaviour |
|---|---|---|
| 0x0 Utility | M2-104-UM §3 | not forwarded; bridge synthesises its own JR Timestamp heartbeat if enabled |
| 0x1 System Real Time | M2-104-UM §4 | forwarded with group rewrite |
| 0x2 MIDI 1.0 Channel Voice | M2-104-UM §6 | forwarded with group rewrite (also the destination for MIDI 1.0 alt 0 byte-stream uplift) |
| 0x3 SysEx7 | M2-104-UM §8 | forwarded with group rewrite |
| 0x4 MIDI 2.0 Channel Voice | M2-104-UM §7 | forwarded with group rewrite (NoteOn / NoteOff / CC / PitchBend / Pressure / Per-Note) |
| 0x5 SysEx8 / Mixed Data | M2-104-UM §9 | forwarded with group rewrite |
| 0xD Flex Data | M2-104-UM §10 | forwarded with group rewrite (Tempo, Time Sig, Key Sig, Metronome, Chord, Clip markers) |
| 0xF UMP Stream | M2-104-UM §11 | NOT forwarded; bridge owns Endpoint + FB Discovery on both sides |

MIDI-CI: Discovery + Process Inquiry are answered locally by the bridge's `m2ci` (PC-facing) and initiated outbound by `m2host` (upstream-facing), per `m2bridge`'s composition.

### What this recipe does NOT cover (and why)

- **Upstream Stream messages forwarded to PC**: bridge owns its own multi-FB topology and Endpoint Name, so passing the upstream's Stream messages through would create a malformed view at the PC. Each side answers Discovery locally.
- **MIDI 1.0 byte-stream forwarded to upstream**: this recipe forwards in the upstream-to-PC direction only. PC-to-upstream traffic is left for a future iteration of `m2bridge` (the API surface allows it; the MT routing logic is the missing piece).

## What lives where

```
midi2cpp/examples/esp32-p4-devkit-bridge2-midi2/
├── README.md
├── board/                              board photos / pinout (TBD)
└── idf/
    ├── CMakeLists.txt                  ESP-IDF project root
    ├── partitions.csv                  single-app, 16 MB flash
    ├── sdkconfig.defaults              target esp32p4, UART stdio, custom partition table
    ├── scripts/fetch_tinyusb.sh        bootstrap: clones the experiment/midi-coexistence branch
    ├── external/                       (gitignored, populated by fetch_tinyusb.sh; symlinked from the host sibling)
    ├── components/tinyusb/
    │   ├── CMakeLists.txt              shim: registers the selected sources (device + host)
    │   └── usb_descriptors.c           PID 0x4095, Product "ESP32P4Bridge2"
    └── main/
        ├── CMakeLists.txt              idf_component_register, pulls midi2cpp + midi2_bridge from ../../../../src
        ├── idf_component.yml           managed deps (none beyond ESP-IDF >=5.4)
        ├── tusb_config.h               16 groups, 4 function blocks, FS device + HS host, USER_RESPONDER on
        ├── esp32_p4_devkit_bridge2.h     public API of the platform glue
        ├── esp32_p4_devkit_bridge2.cpp   USB-OTG dual PHY init + TinyUSB tasks + write fns + mount cbs
        └── main.cpp                    identity setup + main loop, ~70 lines
```

## License

MIT, inherits the parent [`midi2cpp` LICENSE](../../LICENSE). TinyUSB (cloned on demand into `idf/external/tinyusb` from the [`experiment/midi-coexistence`](https://github.com/sauloverissimo/tinyusb/tree/experiment/midi-coexistence) branch on top of upstream master) is MIT.

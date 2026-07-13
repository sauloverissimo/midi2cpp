# [midi2cpp](../..) | Bridge MIDI 2.0
## Waveshare ESP32-P4-WIFI6-DEV-KIT

Dual-stack USB MIDI 2.0 bridge on the **Waveshare ESP32-P4-WIFI6-DEV-KIT**. Runs TinyUSB host on the USB-A jacks (UTMI PHY, OTG_HS controller at 480 Mbps, rhport 1) and TinyUSB device on the **USB-Device** USB-C jack (INT PHY, OTG_FS controller, rhport 0) in the same firmware, forwarding MIDI 2.0 channel-voice traffic from any upstream device into the host PC's view of `ESP32-P4 Bridge MIDI 2.0`. Mixed-protocol bus: MIDI 1.0 controllers (Arturia, M-Audio, generic synths) coexist with MIDI 2.0 devices on the same USB-A hub, each routed through its own class driver. ESP-IDF v5.4 build, no Arduino IDE.

![esp32-p4-devkit-bridge-midi2 banner](board/banner.png)

> Built against the TinyUSB [`experiment/midi-coexistence`](https://github.com/sauloverissimo/tinyusb/tree/experiment/midi-coexistence) branch on top of upstream master. Running `CFG_TUH_MIDI=CFG_TUH_MIDI2=1` requires a tie-breaker between the legacy and MIDI 2.0 host class drivers (both match the same Audio + MIDIStreaming class triple); the branch adds an alt-walk `bcdMSC` defer (~85 lines, gated by `#if CFG_TUH_MIDI2` and `#if !CFG_TUH_MIDI2_LEGACY_FALLBACK`) plus an opt-in user responder (`CFG_TUD_MIDI2_USER_RESPONDER`) for per-FB group windows and dynamic FB Names. Staged as follow-up PRs upstream.

## Topology

```
                                  ┌───────────────────────────────────────┐
PC / DAW ─── USB-Device USB-C ───►│ Waveshare ESP32-P4-WIFI6-DEV-KIT      │
              (INT PHY, OTG_FS,   │   rhport 0  m2device + m2ci (responder)│
               LP_SYS swap)       │      ▲                                │
                                  │      │ typed-callback forwarding      │
                                  │      ▼                                │
                                  │   rhport 1  m2host (auto-discovery)   │
                                  └───────────────────────────────────────┘
                                          ▲
                                          │ USB-A (UTMI PHY, OTG_HS)
                                          │ via onboard CH334F hub
                                          │
                                  MIDI 2.0 + MIDI 1.0 devices
                                  up to 4 simultaneous via the hub
```

## MIDI 1.0 + MIDI 2.0 host coexistence

The legacy `midi_host.c` and the new `midi2_host.c` (merged upstream via PR #3571) both match the same Audio + MIDIStreaming class triple, so without a tie-breaker the order of `usbh_class_drivers[]` decides who claims each upstream interface (legacy wins, dropping MIDI 2.0 traffic to byte-stream). The `experiment/midi-coexistence` branch on top of upstream master adds an alt-walk `bcdMSC` defer so each driver only claims interfaces that match its own protocol version. With the fix in place, plugging an Arturia MiniLab 25 (MIDI 1.0) and an ESP32-S3 device (MIDI 2.0) into the same hub gives one mount per device, each routed through its own class driver. The bridge forwards `tuh_midi2_*` typed callbacks through `m2device` to the PC and instruments the legacy callbacks with diagnostic prints (no MIDI 1.0 forwarding yet). Staged as a follow-up PR upstream.

## USB identity

What the PC sees on the device side (USB-Device USB-C jack):

| Field | Value |
|---|---|
| VID:PID | `cafe:4092` (development-only) |
| Product | `ESP32-P4 Bridge MIDI 2.0` |
| Manufacturer | `midi2.diy` |

The bridge also runs an `m2host` instance for the upstream side, with its own MUID seeded from `esp_random()` masked to 28 bits, so the bridge is a CI Initiator towards upstream devices and a CI Responder towards the PC at the same time.

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

![esp32-p4-devkit-bridge-midi2 banner](board/hardware.png)

| Connector / Pin | Use |
|---|---|
| USB-C **USB-Device** | INT device PHY (OTG_FS), routed to the PC. Mandatory `LP_SYS.usb_ctrl` PHY swap applied at boot to route OTG_FS to PHY0 (GPIO24 / GPIO25) |
| USB-A jacks (×2) | UTMI host PHY (OTG_HS, 480 Mbps), routed through onboard CH334F USB hub. Plug upstream MIDI 1.0 / 2.0 devices here |
| USB-C **ToUART** | CH343 USB-Serial-JTAG bridge, console stdio @ 115200 8N1, flashing |
| BOOT button | Hold during reset to enter download mode (rarely needed; CH343 auto-reset handles it) |
| RESET button | Reboot |

## Validation

Plug the **USB-Device** USB-C into the host PC. Plug any USB MIDI 2.0 device into either USB-A jack. The PC should enumerate `cafe:4092 ESP32-P4 Bridge MIDI 2.0` and expose a MIDI 2.0 endpoint.

- **Linux**: `lsusb | grep cafe:4092` shows `ESP32-P4 Bridge MIDI 2.0`. `amidi -l` lists the bridge's MIDI 2.0 group. `aseqdump -p <bridge-port>` shows the upstream device's NoteOn / NoteOff / CC / PitchBend in real time.
- **Windows**: Microsoft MIDI Services Console shows `ESP32-P4 Bridge MIDI 2.0` with Native data format = UMP, MIDI 2.0 Protocol = True.
- **macOS**: Audio MIDI Setup shows `ESP32-P4 Bridge MIDI 2.0`.

The UART console mirrors the events:

```
[boot] esp32-p4-devkit-bridge-midi2
Host UTMI PHY ready (rhport 1)
Device INT PHY ready, full speed (rhport 0)
Both TinyUSB tasks started (device on core 0, host on core 1)
[bridge] PC sees ESP32-P4 Bridge MIDI 2.0 (cafe:4092)

[host] device idx=0 connected, alt=1 (UMP)
[ep] idx=0 UMP v1.1, 1 FB, MIDI2=1
[ep] idx=0 Endpoint Name: <upstream-device-name>
[fwd idx0] NoteOn ch=0 note=64 vel=0x8000
[fwd idx0] CC ch=0 #74 val=0x12345678
```

The CH334F hub allows up to 4 MIDI 2.0 devices simultaneously (one direct + a 3-port external hub).

## Spec coverage

Full UMP pass-through bridge.

| UMP MT | Direction | Spec | Notes |
|---|---|---|---|
| 0x4 MIDI 2.0 Channel Voice | upstream→PC | M2-104-UM §7 | NoteOn / Off (16-bit velocity), CC (32-bit), Pitch Bend (32-bit), Channel Pressure, Poly Pressure, Per-Note Pitch Bend, Program with Bank, all forwarded |
| 0xF UMP Stream | both | M2-104-UM §11 | Endpoint Discovery answered locally on each side, not proxied |

UMP Stream Discovery and MIDI-CI traffic are answered locally on each side, not forwarded across the bridge.

## Showcase

![esp32-p4-devkit-bridge-midi2 banner](monitor/stack.png)

Console output during operation:

| Event | Console line |
|---|---|
| Boot | `[boot] esp32-p4-devkit-bridge-midi2` |
| PHY init | `Host UTMI PHY ready (rhport 1)` / `Device INT PHY ready, full speed (rhport 0)` |
| Device-side ready | `[bridge] PC sees ESP32-P4 Bridge MIDI 2.0 (cafe:4092)` |
| Mount upstream | `[host] device idx=N connected, alt=A (UMP\|byte-stream)` |
| Endpoint Info | `[ep] idx=N UMP vM.m, F FB, MIDI2=1` |
| Endpoint Name | `[ep] idx=N Endpoint Name: <product>` |
| NoteOn forwarded | `[fwd idxN] NoteOn ch=C note=N vel=0xVVVV` |
| NoteOff forwarded | `[fwd idxN] NoteOff ch=C note=N vel=0xVVVV` |
| CC forwarded | `[fwd idxN] CC ch=C #I val=0xVVVVVVVV` |
| Pitch Bend forwarded | `[fwd idxN] PitchBend ch=C val=0xVVVVVVVV` |
| Disconnect | `[host] device idx=N disconnected` |

Channel Pressure, Poly Pressure, Per-Note Pitch Bend, and Program Change with bank are also forwarded but do not log to console (re-enable in `idf/main/main.cpp` if needed).

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE).

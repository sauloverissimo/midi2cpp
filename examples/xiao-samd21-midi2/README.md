# [midi2cpp](../..) | Device MIDI 2.0
## Seeed Studio XIAO SAMD21

USB MIDI 2.0 device on the [**XIAO SAMD21**](https://wiki.seeedstudio.com/Seeeduino-XIAO/) (Cortex-M0+, 32 KB SRAM). Native CMake build via TinyUSB's `family_support.cmake`, no Arduino IDE.

![xiao-samd21-midi2 banner](board/banner.png)

## USB identity

| Field | Value |
|---|---|
| VID:PID | `cafe:40F0` (development-only) |
| Product | `XIAO SAMD21 MIDI 2.0` |
| Manufacturer | `midi2.diy` |

![xiao-samd21-midi2 banner](board/hardware.png)

## Build

Requires CMake 3.20+, `arm-none-eabi-gcc`, Python 3.

```bash
cmake -B build         # first run fetches TinyUSB + SAMD21 SDK
cmake --build build -j
```

Convert to UF2:

```bash
python3 build/_deps/tinyusb-src/tools/uf2/utils/uf2conv.py \
    -c -b 0x2000 -f SAMD21 \
    -o build/xiao-samd21-midi2.uf2 build/xiao-samd21-midi2.bin
```

Pointing at a local TinyUSB checkout: `cmake -B build -DTINYUSB_PATH=/path/to/tinyusb`.

## Flash

The XIAO has no Reset button, only two RST pads at the USB-C end. Enter UF2 bootloader by:

- bridging the RST pads twice within 500 ms, or
- `stty -F /dev/ttyACM<N> 1200; sleep 1` (1200 bps touch).

Drop `build/xiao-samd21-midi2.uf2` on the mounted `Arduino` drive. Or use bossac:

```bash
sudo bossac -i -d --port=/dev/ttyACM<N> --offset=0x2000 -w -v build/xiao-samd21-midi2.bin -R
```

## Validation

```bash
lsusb | grep cafe:40F0
amidi -l                        # IO  hw:N,1,0  Group 1 (Main)
PORT=$(aseqdump -l | grep -i XiaoSAMD21 | awk '{print $1}' | tr -d ':')
timeout 8 aseqdump -p ${PORT}   # chromatic walk C4..G#4
```

## Spec coverage

Minimal core plus the standard MIDI-CI surface (Discovery, Property Exchange with DeviceInfo/ChannelList/ProgramList, Process Inquiry); static resources live in flash, so the SAMD21 SRAM budget is unaffected. The full UMP + MIDI-CI surface on a SAMD21-class chip belongs to the upcoming `xiao-samd51-midi2` (4× the SRAM).

| UMP MT | Spec | Notes |
|---|---|---|
| 0x0 Utility | M2-104-UM §3 | JR heartbeat, 500 ms |
| 0x4 MIDI 2.0 Channel Voice | M2-104-UM §7 | NoteOn/Off + 32-bit CC #74 sweep |
| 0xF UMP Stream | M2-104-UM §10 | Endpoint Discovery, Device Identity, Endpoint Name, Product Instance ID, Stream Config Notify, FB Info, FB Name |

MIDI-CI: Discovery + Property Exchange (DeviceInfo, ChannelList, ProgramList + built-in ResourceList) + Process Inquiry, via the `m2ci` responder.

## Showcase
![xiao-samd21-midi2 banner](monitor/stack.png)

Always on while mounted: JR heartbeat (500 ms), UMP Stream + MIDI-CI Discovery responders, PA17 LED lit.

Per cycle (~6.5 s):

| Window | Detail |
|---|---|
| 0 to 4.0 s | Chromatic walk C4 → G#4 (8 steps, 500 ms each), 16-bit velocity ramp `0x2000` → `0xFFFF`, 32-bit CC #74 sweep `0x20000000` → `0xFFFFFFFF` |
| 4.0 to 4.5 s | Final NoteOff |
| 4.5 to 6.5 s | Gap |

## License

MIT, inherits parent [`midi2cpp` LICENSE](../../LICENSE).

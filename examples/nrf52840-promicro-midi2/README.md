# [midi2_cpp](../..) | Device MIDI 2.0
## Pro Micro nRF52840 (Nice!Nano class)

Tier B standard-subset USB MIDI 2.0 device example for **Pro Micro nRF52840** class boards (Nice!Nano, BlueMicro840, FYSETC nRF52840 Pro Micro, generic Pro Micro nRF52840 clones). nRF52840 Cortex-M4F at 64 MHz, 256 KB SRAM, 1 MB flash, native USB FS. Native CMake build via TinyUSB's `family_support.cmake`, ARM GNU toolchain, no Arduino IDE involvement. Lives at `midi2_cpp/examples/nrf52840-promicro-midi2/` and consumes the parent `midi2_cpp` library directly via `../../src`.

![nrf52840-promicro-midi2 banner](board/board.png)

> ⚠️ **TinyUSB override, not yet upstream.** The USB MIDI 2.0 device class driver this recipe depends on lives in TinyUSB [PR #3571](https://github.com/hathach/tinyusb/pull/3571), still under review. Until that PR merges into `hathach/tinyusb`, the build pulls a personal fork ([`sauloverissimo/tinyusb`](https://github.com/sauloverissimo/tinyusb)) at a pinned SHA via CMake FetchContent. Treat as **beta**; when the PR lands the override goes away.

PID `0x40F1` distinguishes this device from the other midi2_cpp recipes; window `0x40D0..0x40EF` (other Tier 2, TinyUSB native CMake on SAMD/nRF52/STM32/RA).

## What this is

`nrf52840-promicro-midi2` is a sibling of [`xiao-samd21-midi2`](../xiao-samd21-midi2/) on the same TinyUSB native CMake build path, swapping the Seeed XIAO SAMD21 for any nRF52840 Pro Micro class board. Both recipes share the same five midi2_cpp platform hooks, the same FetchContent-based pull of the TinyUSB PR #3571 fork, and the same skill template (`tier-2-tinyusb-native-cmake`).

The recipe owns:
- TinyUSB BSP `feather_nrf52840_express` (already upstream in TinyUSB fork)
- Nordic nrfx SDK + ARM CMSIS_5 (auto-fetched via `tools/get_deps.py nrf` at first configure)
- USB descriptors (VID `0xCAFE`, PID `0x40F1`, Product "Nrf52840ProMicro")
- midi2_cpp C++17 wrapper compiled against the nRF52840 (Cortex-M4F thumb) target
- The five midi2_cpp platform hooks: `setWriteFn`, `feedRx`, `setNowFn`, `setMounted` / `setAltSetting`, `CI::setRngFn`
- LED activity via the BSP (P1.15 on Feather Express; not visible on most generic Pro Micro clones — see **Hardware** below)

After `nrf52840_promicro_midi2::init(midi, ci)`, the application sees only `m2device` and `m2ci`. It never touches `tud_*`, `board_*`, or any TinyUSB symbol directly.

### Why `feather_nrf52840_express` BSP for a generic Pro Micro

TinyUSB upstream does NOT carry a Pro Micro nRF52840 / Nice!Nano BSP. The Feather Express BSP is the nearest match because all three of the things that matter for this recipe line up:

1. **Same MCU**: nRF52840 Cortex-M4F, native USB FS peripheral.
2. **Same flash layout**: linker `nrf52840_s140_v6.ld` puts FLASH ORIGIN at `0x26000`, matching the Adafruit nRF52 UF2 bootloader + SoftDevice S140 v6.x region the Nice!Nano shipping image uses.
3. **Same SoftDevice RAM reservation**: RAM ORIGIN at `0x20003400`, leaving the lower 12 KB for S140 v6.x even though this recipe runs `noos` and never calls into SoftDevice.

The only divergence is the on-board LED pin (Feather: P1.15 with `LED_STATE_ON=1`; typical Pro Micro clones: P1.06 / P1.07 with active-low). The activity LED may light a non-visible GPIO on the generic Pro Micro, but USB enumeration and MIDI streaming are unaffected. A future `nice_nano` BSP (already in community discussion for upstream) would replace this BSP one-for-one without touching the rest of the recipe.

## What this is not

Not a finished product. The bundled `nrf52840-promicro-midi2` executable is a smoke-test demo (Per-Note Pitch Bend vibrato + chromatic walk + RPN/NRPN burst + Discovery responder + JR heartbeat). Real applications copy this core and add their own behaviour:

- A Nice!Nano-based wireless keyboard could pair this MIDI 2.0 USB surface with BLE-MIDI 2.0 over the radio (the SoftDevice S140 v6 is already on flash).
- A Pro Micro hand controller could expose its capacitive touch / encoder / pot inputs as Per-Note Pitch Bend / Per-Note Controllers / 32-bit CCs.
- A bridge variant could wrap the nRF52840 USB device side around an external host (MAX3421E + the BSP's MAX3421 pins on the Feather Express, or a peer transport over UART / SPI on bare Pro Micros).

## Identification

| Field | Value |
|---|---|
| USB VID | `0xCAFE` (TinyUSB educational, development-only) |
| USB PID | `0x40F1` |
| USB Manufacturer | `github.com/sauloverissimo` |
| USB Product | `Nrf52840ProMicro` |
| USB Serial (fallback) | `Nrf52840ProMicro-0001` |
| Endpoint Name | `Nrf52840ProMicro` |
| Product Instance ID | `Nrf52840ProMicro-showcase-0001` |
| Function Block Name | `Main` |
| MIDI-CI Manufacturer ID | `{0x7D, 0x00, 0x00}` (MIDI Association educational/non-commercial prefix) |
| MIDI-CI Family / Model / Version | `0x0001 / 0x0001 / 0x00010000` |

> **VID `0xCAFE` is development-only.** Production firmware MUST replace both `idVendor` and `idProduct` with a real allocation (`0x1209` pid.codes, `0x16C0` V-USB, or a purchased USB-IF VID).

## Build

Requirements:

- **CMake 3.20+**
- **arm-none-eabi-gcc** (Arm GNU embedded toolchain, 9+ recommended; tested with 13.2.1)
- **Python 3** (for TinyUSB's `tools/get_deps.py` to fetch the Nordic nrfx SDK + CMSIS_5 at first configure)
- Internet on the first `cmake -B build` (FetchContent of the TinyUSB fork + first run of get_deps.py)

```bash
git clone https://github.com/sauloverissimo/midi2_cpp.git
cd midi2_cpp/examples/nrf52840-promicro-midi2
cmake -B build         # first run fetches TinyUSB fork + nRF deps
cmake --build build -j # offline from here on
```

Output: `build/nrf52840-promicro-midi2.elf` + `build/nrf52840-promicro-midi2.bin` + `build/nrf52840-promicro-midi2.hex`. UF2 conversion is a separate step (TinyUSB's `family_support.cmake` does not run uf2conv automatically):

```bash
python3 build/_deps/tinyusb_fork-src/tools/uf2/utils/uf2conv.py \
    -c -b 0x26000 -f 0xADA52840 \
    -o build/nrf52840-promicro-midi2.uf2 build/nrf52840-promicro-midi2.bin
```

The `0x26000` offset matches the Adafruit nRF52 UF2 bootloader region; firmware lives above the bootloader + SoftDevice S140 v6 reservation. `0xADA52840` is the Adafruit nRF52840 family identifier accepted by `uf2conv.py` (alias `ADAFRUIT_NRF52840`).

To point at a working copy of the TinyUSB fork already on disk:

```bash
cmake -B build -DTINYUSB_FORK_PATH=/path/to/your/tinyusb
```

## Flash

Pro Micro nRF52840 boards (Nice!Nano and clones) ship with the [Adafruit nRF52 UF2 bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader) pre-flashed. Two flash paths:

### Drag-and-drop (UF2)

1. Enter bootloader on the board. Two ways:
   - **Double-tap RST**: tap the on-board RST button (or short the RST pad to GND on bare Pro Micros) twice in quick succession (interval < 500 ms). Board re-enumerates as `NICENANO` UF2 mass-storage with VID:PID `239a:00b3`.
   - **1200 bps touch**: `stty -F /dev/ttyACM<N> 1200; sleep 1`. The Adafruit nRF52 bootloader reacts to a 1200 bps open by jumping to bootloader. This works without touching the hardware.
2. Mount the `NICENANO` drive (most desktop file managers auto-mount; otherwise `udisksctl mount -b /dev/sd<x>`).
3. Copy `build/nrf52840-promicro-midi2.uf2` to the mounted drive. Board auto-reboots into the firmware. After ~3 s, `lsusb | grep cafe:40f1` should show `Nrf52840ProMicro`.

### adafruit-nrfutil (binary upload, alternative)

```bash
# Enter bootloader first (double-tap RST or 1200 bps touch as above)
adafruit-nrfutil --verbose dfu serial \
    --package build/nrf52840-promicro-midi2.zip \
    -p /dev/ttyACM<N> -b 115200 --singlebank --touch 1200
```

### Empty flash (no bootloader)

If the board is brand new and `lsusb` shows nothing on plug-in, flash the [Adafruit nRF52 bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader) first via SWD. Four wires (SWDIO + SWDCLK + GND + 3V3) and any of: J-Link, DAPLink, Raspberry Pi Pico running picoprobe firmware, ST-Link with the Black Magic Probe firmware. Then:

```bash
git clone --recursive https://github.com/adafruit/Adafruit_nRF52_Bootloader
cd Adafruit_nRF52_Bootloader
make BOARD=nice_nano flash-pyocd      # or flash-jlink, flash-openocd
```

After this, the bootloader sticks; from now on the UF2 drag-and-drop path works.

## Hardware

| Pin | Use |
|---|---|
| USB-C / micro-USB | USB FS device interface (MIDI 2.0) |
| RST button (or pad) | Double-tap to enter UF2 bootloader |
| P1.15 (Feather BSP `LED_PIN`) | Activity LED in the recipe; **not exposed on most generic Pro Micro clones** |
| P1.06 / P1.07 (typical clone LEDs) | Visible LEDs on chinese Pro Micro nRF52840 boards; ignored by this recipe |
| GPIO breakouts (D0 to D31, A0 to A7) | Free for application use |

The mismatch between the BSP `LED_PIN` (P1.15) and the visible LEDs on a generic Pro Micro clone (P1.06 / P1.07) is cosmetic. To get a visual mount indicator on a clone, edit `src/nrf52840_promicro_midi2.cpp::led_show_mounted` and drive the right pin via `nrf_gpio_pin_set` / `nrf_gpio_pin_clear` instead of `board_led_write`. This recipe leaves the BSP default in place to match Feather Express deployments and to keep the diff minimal.

The MCU silicon datasheet ([nRF52840 Product Specification](https://infocenter.nordicsemi.com/topic/ps_nrf52840/keyfeatures_html5.html)) is shared across every nRF52 recipe and is hosted on Nordic's Infocenter.

## Spec coverage

**Tier B** (standard subset). Hardware-bracket reference for nRF52840-class chips with 256 KB SRAM and 1 MB flash. Same Tier as the Adafruit Feather RP2040 device sibling.

### What this recipe emits and demonstrates

| UMP MT | Transport | Spec section | Showcase Scene | Notes |
|---|---|---|---|---|
| 0x0 Utility | USB | M2-104-UM §3 | JR heartbeat | 500 ms periodicity |
| 0x4 MIDI 2.0 Channel Voice | USB | M2-104-UM §7 | Vibrato (Per-Note PB) + chromatic walk (NoteOn/Off + 32-bit CC #74) + RPN/NRPN/RelRPN/RelNRPN | sub-statuses 0x2 (RPN), 0x3 (NRPN), 0x4 (RelRPN), 0x5 (RelNRPN), 0x6 (Per-Note PB), 0x8 (CC), 0x9 (NoteOff), 0x9 (NoteOn) |
| 0xF UMP Stream | USB | M2-104-UM §10 | (responder, not a Scene) | Endpoint Discovery, Device Identity, Endpoint Name, Product Instance ID, Stream Config Notify, FB Info, FB Name |

### MIDI-CI surface (M2-101-UM)

| Subsystem | Coverage |
|---|---|
| Discovery (Initiator + Responder) | responder: yes (MUID, Manufacturer, Family, Model, Version) |
| Profile Configuration | not advertised (Tier B drop) |
| Property Exchange | not advertised (Tier B drop) |
| Process Inquiry | not advertised (Tier B drop) |

### What this recipe does NOT cover (and why)

- **MT 0x3 SysEx7 / MT 0x5 SysEx8**: scope drop. The nRF52840 has the SRAM headroom, but the recipe stays focused on Channel Voice + Stream Discovery. A future `nrf52840-sysex-bench` variant could add SysEx out + reassembly stress.
- **MT 0xD Flex Data**: scope drop. A future variant could opt into Tempo + Time Sig + Chord Name.
- **Property Exchange storage + Process Inquiry advertising**: not advertised at the MIDI-CI layer; nRF52840 SRAM can afford it, but the recipe keeps to Tier B parity with the Feather RP2040 device sibling.
- **MIDI 2.0 Initiator role for CI**: this is a device-side responder; an Initiator demo lives in the project's host recipes.
- **BLE-MIDI 2.0**: out of scope for this recipe. The Nice!Nano's S140 v6 SoftDevice is already on flash, so a follow-up `nice-nano-ble-midi2` could enable BLE alongside USB.

For full Tier A coverage on an nRF52-class chip, the future `nrf5340-bench` recipe (Phase 2; Cortex-M33 application core + dedicated network core) is the canonical target.

## Showcase

What the bundled `nrf52840-promicro-midi2` executable demonstrates after enumeration, while `usb_midi2.altSetting() == 1`:

**Always-on:**

- **JR Timestamp heartbeat** every 500 ms (MT 0x0 status 0x2)
- **UMP Stream Discovery responder** (full Endpoint + FB Discovery surface)
- **MIDI-CI Discovery responder** (MUID, Manufacturer, Family, Model, Version)
- **Activity LED on P1.15** lit while mounted (Feather BSP default; not visible on most clones)

**Per cycle (~13 s):**

| Window | Detail |
|---|---|
| 0 to 3.6 s | Sustained C4 with Per-Note Pitch Bend vibrato (5 Hz, +/- half a semitone, 50 ms update period) |
| 3.6 to 7.6 s | Chromatic walk C5 to G#5 (8 steps, 500 ms each), 16-bit velocity ramp `0x2000` to `0xFFFF`, 32-bit CC #74 sweep |
| 7.6 to 8.2 s | Final NoteOff |
| 8.2 to 10.6 s | RPN 0/0 (Pitch Bend Sensitivity), NRPN 0x12/0x34, Relative RPN +delta, Relative NRPN -delta (one each, 600 ms apart) |
| 10.6 to 12.6 s | Gap before next cycle |

## Validation

Hardware steps (Linux):

1. Flash via UF2 drag-and-drop or `adafruit-nrfutil` per the **Flash** section above.
2. Confirm enumeration:
   ```bash
   lsusb | grep cafe:40f1
   # expected: Bus 001 Device NN: ID cafe:40f1 github.com/sauloverissimo Nrf52840ProMicro
   amidi -l
   # expected: IO  hw:N,1,0  Group 1 (Main)
   ```
3. Capture UMPs:
   ```bash
   PORT=$(aseqdump -l | grep -i Nrf52840 | awk '{print $1}' | tr -d ':')
   timeout 15 aseqdump -p ${PORT}
   # expected: NoteOn C4 -> Per-Note PB stream -> NoteOff C4 ->
   # NoteOn/Off C5..G#5 sequence with rising velocity ->
   # RPN/NRPN/RelRPN/RelNRPN burst -> 2 s gap -> repeat
   ```
4. Pair with a known-good MIDI 2.0 host recipe ([`adafruit-feather-rp2040-host-midi2`](../adafruit-feather-rp2040-host-midi2/)) for cross-validation: plug the Pro Micro into the Feather's USB-A, the Feather's OLED should display `[0] MIDI 2.0` with the showcase events scrolling.

## What lives where

```
midi2_cpp/examples/nrf52840-promicro-midi2/
├── README.md                                this file
├── CMakeLists.txt                           FetchContent TinyUSB fork + family_support
├── board/
│   ├── board.png                            board photo
│   └── pinout.png                           Pro Micro nRF52840 pinout (silkscreen "ProMicro nRF52840")
├── monitor/                                 bench / Microsoft MIDI Console captures (TBD)
└── src/
    ├── main.cpp                             showcase entry, Tier B demo
    ├── nrf52840_promicro_midi2.{h,cpp}      board glue: board_init + tusb_init + hooks
    ├── usb_descriptors.c                    VID/PID + descriptors
    └── tusb_config.h                        CFG_TUD_MIDI2 + endpoint sizes
```

The TinyUSB PR #3571 fork is fetched into `build/_deps/tinyusb_fork-src` (gitignored) on first configure. The Nordic nrfx SDK and ARM CMSIS_5 are fetched into the same tree under `hw/mcu/nordic/nrfx/` and `lib/CMSIS_5/` by `tools/get_deps.py nrf` (also auto-triggered at configure time).

## License

MIT, inherits the parent [`midi2_cpp` LICENSE](../../LICENSE). The TinyUSB fork (fetched on demand) is MIT (upstream by hathach, fork by sauloverissimo carrying the MIDI 2.0 class drivers from the still-open [PR #3571](https://github.com/hathach/tinyusb/pull/3571)). The Nordic nrfx SDK is BSD-3-Clause (Nordic Semiconductor ASA). The Adafruit nRF52 UF2 bootloader (referenced from the **Flash** section, not vendored) is GPL-3.0 (Adafruit Industries). Board pinout and photo images are © the original board vendors (educational use under fair-use scope of this open-source recipe).

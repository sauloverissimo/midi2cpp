# [midi2cpp](../..) | Conformance bench MIDI 1.0
## Raspberry Pi Pico (RP2040)

Flash-and-read conformance bench for the MIDI 1.0 bytestream to UMP
conversion path. A battery of malformed MIDI 1.0 byte vectors, drawn from
real-world failure reports (driver padding, truncated SysEx, orphan data
bytes, interleaved Real-Time), is converted on-target by the midi2 core and
self-checked against the expected UMP words derived from M2-104-UM. Results
print on the USB serial console. No MIDI wiring involved: the vectors live
in flash. Lives at `midi2cpp/examples/rp2040-bench-midi1/`.

Optionally builds with [AM_MIDI2.0Lib](https://github.com/midi2-dev/AM_MIDI2.0Lib)
fetched at configure time to print both conversions side by side for every
vector.

## What this is

- A conformance oracle you can drag onto any Pico: hold BOOTSEL, copy the
  UF2, open the serial port, read the table.
- The same vector battery that gates midi2 releases (host suite, on-target
  bench, USB wire stress), packaged as a standalone firmware.
- With `-DWITH_AM_MIDI2=ON`, a cross-implementation comparison: the same
  bytes through two independent converters on the same silicon.

## What this is not

- Not a USB MIDI device. The only USB interface is the stdio serial console;
  there is no MIDI identity, no Function Block, no MIDI-CI surface.
- Not a wire test. For the full-path version over real USB, see
  [Level 2](#level-2-the-full-wire-rig) below.

## Build

Requires the Pico SDK (`PICO_SDK_PATH` exported).

```bash
cd midi2cpp/examples/rp2040-bench-midi1
cmake -B build
cmake --build build -j
# build/rp2040-bench-midi1.uf2
```

Side-by-side with AM_MIDI2.0Lib (fetched at configure time, ref selectable):

```bash
cmake -B build -DWITH_AM_MIDI2=ON [-DAM_MIDI2_GIT_TAG=<ref>]
cmake --build build -j
```

Flash: hold BOOTSEL, plug the Pico, copy the UF2. Then open the serial
console (115200) and read; the battery reruns every 5 seconds.

## What it checks

| # | Vector | Rule exercised |
|---|---|---|
| 1-4 | leading/orphan data bytes, `F8` + padding | no message is fabricated from converter state |
| 5 | undefined System Common `F4` | passes alone, next message intact |
| 6 | `F8` inside a SysEx | Real-Time keeps its wire position between packets (M2-104-UM 7.7.1) |
| 7 | duplicate `F7` | absorbed, no phantom packet |
| 8 | `F0` restart with bytes buffered | previous message closes, no payload merge |
| 9-10 | Channel Voice status inside a SysEx | SysEx terminates, closing packet carries the wire bytes, notes survive |
| 11 | `F6` inside a SysEx | closing packet first, Tune Request after, wire order kept |
| 12-13 | Running Status baseline, System Common | Running Status applies to Channel Voice only |

Sample output:

```
PASS CV status ends streamed SysEx
  bytes         : F0 01 02 03 04 05 06 07 08 90 3C 7F
  midi2         : 30160102 03040506 30320708 00000000 20903C7F
```

The self-check verdict applies to the midi2 output. With `WITH_AM_MIDI2=ON`
the second conversion is printed underneath; word streams can differ
legitimately in SysEx packet partitioning, so the comparison is left to the
reader.

## Level 2: the full-wire rig

The bench replays vectors from flash. To watch the same cases cross a real
USB cable, pair the bundled sender with any bridge recipe in this tree:

1. Flash [`sender/promicro-midi1-gabarito/`](sender/promicro-midi1-gabarito/)
   on any ATmega32U4 board (Arduino Leonardo, Pro Micro; MIDIUSB library).
   It enumerates as a pure USB MIDI 1.0 device and loops a deterministic
   stress cycle: 4096 sequence-numbered note pairs, 512 numbered CCs, and
   the malformed vectors above as phase C, with cycle markers on CC 119.
2. Plug it into the host port of a bridge recipe, for example
   [adafruit-feather-rp2040-bridge-midi2](../adafruit-feather-rp2040-bridge-midi2/)
   or [esp32-p4-devkit-bridge2-midi2](../esp32-p4-devkit-bridge2-midi2/).
3. Capture the uplifted UMP on the PC side. Raw capture is recommended
   (`amidi -p hw:X,Y,Z -r out.bin -c -a`); the ALSA sequencer layer can drop
   the front of dense bursts, and `amidi` filters Clock and Active Sensing
   unless `-c -a` are given.

## What lives where

```
rp2040-bench-midi1/
  CMakeLists.txt                     Pico SDK build, WITH_AM_MIDI2 option
  pico_sdk_import.cmake
  src/main.cpp                       vectors, expectations, runners, report
  sender/promicro-midi1-gabarito/    MIDI 1.0 stress sender (Arduino, MIDIUSB)
```

## License

MIT, inherits the parent. AM_MIDI2.0Lib is MIT, fetched at configure time
when enabled, never vendored.

# rp2040-bench-midi1

MIDI 1.0 bytestream to UMP conformance bench. Malformed byte vectors
(truncated SysEx, driver padding, orphan data bytes, interleaved Real-Time)
converted on-target by the [midi2](https://github.com/sauloverissimo/midi2)
core and self-checked against expected words from M2-104-UM. Results on the
USB serial console (115200); no MIDI wiring, the vectors live in flash.

```bash
cmake -B build && cmake --build build -j                       # midi2 only
cmake -B build -DWITH_AM_MIDI2=ON && cmake --build build -j    # side by side with AM_MIDI2.0Lib
```

Hold BOOTSEL, drag `build/rp2040-bench-midi1.uf2` onto the Pico, open the
serial port:

```
PASS CV status ends streamed SysEx
  bytes         : F0 01 02 03 04 05 06 07 08 90 3C 7F
  midi2         : 30160102 03040506 30320708 00000000 20903C7F
  AM_MIDI2.0Lib : 30160102 03040506
  note          : conversions differ, compare the words above
```

`sender/` carries a deterministic MIDI 1.0 stress sender (ATmega32U4,
MIDIUSB) for wire-level testing against any bridge recipe in this tree.

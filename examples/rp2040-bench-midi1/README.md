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
PASS duplicate F7
  bytes         : F0 21 22 23 24 25 F7 F7
  midi2         : 30052122 23242500
  AM_MIDI2.0Lib : 30052122 23242500
```

`sender/` carries a deterministic MIDI 1.0 stress sender (ATmega32U4,
MIDIUSB) for wire-level testing against any bridge recipe in this tree.

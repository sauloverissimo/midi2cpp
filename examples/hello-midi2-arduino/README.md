# [midi2cpp](../..) | Starter
## hello-midi2-arduino

The smallest complete midi2cpp sketch, and the baseline the board recipes grow from. It builds the `m2device` + `m2ci` pair, installs the platform hooks (write, clock, RNG), brings up the full MIDI-CI responder package (Discovery, Profile, Property Exchange with DeviceInfo / ChannelList / ProgramList, Process Inquiry), and loops one NoteOn through its own dispatcher to prove the path.

No USB transport is wired: `plat_write` is a no-op, so the sketch compiles on **any Arduino board with C++17**, even one with no native USB. That makes it the neutral starting point: the sketch is the part of a MIDI 2.0 device that never changes from board to board.

## Build

Arduino IDE: install **midi2cpp** from the Library Manager, open the sketch, pick any board, upload. Serial monitor at 115200 baud prints the dispatched NoteOn.

arduino-cli:

```bash
arduino-cli compile --fqbn <your:board:fqbn> hello-midi2-arduino.ino
```

## Turning it into a real device

Wire the two transport hooks to the USB MIDI 2.0 driver your platform ships, and keep everything else:

- `plat_write`: forward the UMP words to the driver (TinyUSB: `tud_midi2_n_ump_write`; Teensy cores fork: `usbMIDI2.write`).
- `loop()`: drain inbound UMP into `midi.feedRx(words, count)` before `midi.task()`.

The MIDI-CI bootstrap in this sketch is the Workbench-validated baseline used by every certified recipe in this repository; keep the identity fields in sync (the `ci.begin` bytes and the `DeviceInfo` JSON must match). The [board recipes](../../README.md#boards) show the wiring done for each supported board, transport included.

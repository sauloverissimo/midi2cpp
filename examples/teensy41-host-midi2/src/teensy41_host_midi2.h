// midi2cpp / teensy41-host-midi2: USBHost_t36 backend interface.
//
// Wires the Teensy 4.1 native USB host port to midi2::Host. The
// application sees only midi2::Host after init() returns and never
// touches USBHost_t36 symbols directly.
//
// Usage in the .ino:
//   #include "src/teensy41_host_midi2.h"
//   midi2::Host midi;
//   void setup() { teensy41_host::init(midi); ... }
//   void loop()  { teensy41_host::task(midi); ... }

#pragma once

#include <Arduino.h>
#include "midi2cpp.h"

namespace teensy41_host {

// Boots USBHost + the single MIDI device slot, wires the four platform
// hooks (write, now, rng, feedRx via task) into midi2::Host, then calls
// midi.begin(). Call once from setup() after Serial.begin(), before any
// midi.onXxx callback registration that touches state.
void init(midi2::Host& midi);

// Pumps USBHost (myusb.Task()), detects mount/unmount transitions on
// the single slot, drains readUMP() into midi.feedRx, and ticks
// midi.task(). Call every iteration of the main loop.
void task(midi2::Host& midi);

} // namespace teensy41_host

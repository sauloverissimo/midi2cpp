// midi2cpp / teensy41-midi2: Arduino IDE backend
//
// Bridges the Teensy core fork USB MIDI 2.0 device path
// (sauloverissimo/cores branch feature/usb-midi2-descriptors,
// usbMIDI2.read / .write / .altSetting) into midi2::Device.
//
// Usage in the .ino:
//   #include "src/teensy41_midi2.h"
//   teensy41::Backend backend;
//   void setup() { backend.begin(); ... }
//   void loop()  { backend.task(); ... }
//   backend.device().sendNoteOn(...);

#pragma once

#include <Arduino.h>
#include "midi2cpp.h"

namespace teensy41 {

class Backend {
public:
	void begin(uint16_t jrHeartbeatMs = 500);
	void task();
	midi2::Device& device() { return dev_; }

private:
	midi2::Device dev_;
};

} // namespace teensy41

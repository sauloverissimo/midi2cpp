// midi2cpp / teensy41-control-surface: Arduino IDE backend
//
// Bridges the Teensy core fork USB MIDI 2.0 device path
// (sauloverissimo/cores branch feature/usb-midi2-descriptors,
// usbMIDI2.read / .write / .altSetting) into midi2::Device.
//
// The Backend class lives in `namespace teensy41` (the platform), not
// `teensy41_control_surface` (this recipe), because the symbols it
// touches (usbMIDI2.*, millis) are Teensy 4.x platform primitives,
// not control-surface-specific. Other Teensy 4.x recipes copy this
// file verbatim; each Arduino sketch compiles in isolation so there
// is no cross-recipe symbol clash.
//
// Usage in the .ino:
//   #include "src/teensy41_control_surface.h"
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

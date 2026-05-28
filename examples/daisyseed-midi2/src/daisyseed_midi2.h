// midi2cpp / daisyseed-midi2: libDaisy backend
//
// Bridges the libDaisy fork USB MIDI 2.0 transport
// (sauloverissimo/libDaisy branch feat/usb-midi2-transport,
// MidiUsbTransport::SetUmpCallback / Tx, usbd_midi2_alt_setting)
// into midi2::Device.
//
// Usage in main.cpp:
//   #include "daisyseed_midi2.h"
//   daisyseed::Backend backend;
//   int main() { backend.begin(); for(;;) { backend.task(); ... } }
//   backend.device().sendNoteOn(...);

#pragma once

#include "midi2cpp.h"

namespace daisyseed {

class Backend {
public:
    void begin(uint16_t jrHeartbeatMs = 500);
    void task();
    midi2::Device& device() { return dev_; }

private:
    midi2::Device dev_;
};

} // namespace daisyseed

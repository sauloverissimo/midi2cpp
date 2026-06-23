// midi2cpp / daisyseed-host-midi2: libDaisy host backend
//
// Bridges the libDaisy fork USB MIDI 2.0 host transport
// (sauloverissimo/libDaisy branch feat/usb-midi2-transport,
// USBHostHandle + MidiUsbTransport::Config::HOST, SetUmpCallback / Tx)
// into midi2::Host.
//
// The Daisy is the host: a USB MIDI 2.0 device plugged into the USB-A
// jack is enumerated, its UMP traffic is fed to midi2::Host, and the
// host issues Endpoint Discovery + MIDI-CI Discovery to it.
//
// Usage in main.cpp:
//   #include "daisyseed_host_midi2.h"
//   daisyseed_host::Backend backend;
//   int main() {
//     backend.begin();
//     auto& host = backend.host();
//     host.onNoteOn(...);
//     for(;;) { backend.task(); }
//   }

#pragma once

#include "midi2cpp.h"

namespace daisyseed_host {

class Backend {
public:
    void begin();
    void task();
    midi2::Host& host() { return host_; }

private:
    midi2::Host host_;
};

} // namespace daisyseed_host

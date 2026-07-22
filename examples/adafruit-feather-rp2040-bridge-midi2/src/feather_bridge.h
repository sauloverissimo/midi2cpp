/*
 * feather_bridge.h: public API of the dual-stack bridge platform layer.
 *
 * Boots TinyUSB on both rhports of the Adafruit Feather RP2040 USB Host:
 *   rhport 0, native USB device -> 16-group MIDI 2.0 endpoint to the PC
 *   rhport 1, PIO-USB host      -> enumerates the upstream device on USB-A
 *
 * All bridge behaviour (slot placement, group windows, Stream Discovery,
 * MIDI-CI faces, MIDI 1.0 uplift) lives in midi2::m2bridge; this layer
 * wires TinyUSB callbacks into it and keeps two board specifics: the
 * USB-A power gate and the hot-swap watchdog (tuh re-init after the
 * upstream has been gone for MIDI2CPP_BRIDGE_WATCHDOG_MS).
 */
#pragma once

#include <cstdint>

#include "midi2cpp.h"
#include "midi2_bridge.h"

namespace feather_bridge {

// Boot both USB stacks and wire TinyUSB to the bridge. Call once,
// after the bridge's identity setters and before the main loop.
void init(midi2::m2bridge& bridge);

// One main-loop tick: pumps both USB stacks, drains the device-side
// RX ring into the bridge, runs the watchdog, ticks the bridge.
void task(midi2::m2bridge& bridge);

// True when the upstream USB-A device is enumerated.
bool upstream_present();

// True when the PC has the device side mounted in UMP mode.
bool downstream_present();

// Send a UMP message (1..4 words) directly to the PC, bypassing the
// slot windows; the standalone showcase uses it on the bridge's own
// Function Block groups. Returns false when the PC side is not ready.
bool send_to_pc(const uint32_t* words, uint8_t count);

}  // namespace feather_bridge

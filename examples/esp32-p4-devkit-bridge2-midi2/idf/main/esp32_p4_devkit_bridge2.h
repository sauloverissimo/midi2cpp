/*
 * esp32_p4_devkit_bridge2.h, platform glue for the m2bridge-based
 * variant of the ESP32-P4 dual-stack USB MIDI 2.0 bridge.
 *
 * The application sees only midi2::m2bridge after init() returns; this
 * header declares the four hooks the platform layer needs from the
 * board (PHY init, TinyUSB device + host tasks, write callbacks wired
 * into the bridge, mount-event forwarding into the bridge slot table).
 */
#pragma once

#include "midi2cpp.h"

namespace esp32_p4_devkit_bridge2 {

// Boots both PHYs (UTMI host first, then INT device with the
// LP_SYS.usb_ctrl swap), spawns the TinyUSB host + device tasks, and
// wires the platform write callbacks + mount lifecycle into the supplied
// midi2::m2bridge. Must be called once from app_main(), before any
// bridge.* call other than identity setters.
void init(midi2::m2bridge& bridge);

// Mirror device-side mount/alt state into the bridge and pump the host
// RX FIFO into bridge.feedHostRx for every mounted upstream MIDI 2.0
// device. Call once per main-loop iteration. The bridge.task() call is
// done internally so the platform layer keeps a single entry point.
void task(midi2::m2bridge& bridge);

}  // namespace esp32_p4_devkit_bridge2

/*
 * esp32_p4_devkit_bridge.h, public API of the
 * esp32-p4-devkit-bridge-midi2 board core.
 *
 * Dual-stack: USB MIDI 2.0 device on rhport 0 (INT PHY, USB-Device
 * USB-C jack) and USB MIDI 2.0 host on rhport 1 (UTMI PHY, USB-A
 * jacks). The application layer never touches tud_, tuh_ or esp_
 * symbols; after init() it only sees the supplied m2device + m2ci +
 * m2host objects.
 */
#pragma once

#include "midi2_cpp.h"

namespace esp32_p4_devkit_bridge {

// Boots both PHYs (UTMI host first, then INT device with the
// LP_SYS.usb_ctrl swap), spawns the TinyUSB host + device tasks, and
// wires the platform hooks into the supplied m2 objects. Must be
// called once from app_main(), before any midi.* / ci.* / host.*
// calls.
void init(midi2::m2device& midi, midi2::m2ci& ci, midi2::m2host& host);

// Drains both stacks: pumps device-side RX into midi.feedRx, host-side
// RX into host.feedRx, refreshes mounted/alt state on the device side,
// and lets midi.task / host.task run their housekeeping. Call every
// iteration of the main loop.
void task(midi2::m2device& midi, midi2::m2host& host);

}  // namespace esp32_p4_devkit_bridge

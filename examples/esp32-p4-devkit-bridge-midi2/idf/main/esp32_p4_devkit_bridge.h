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

#include "midi2cpp.h"

namespace esp32_p4_devkit_bridge {

// Multi-slot bridge layout: each upstream device occupies a 4-group
// window on the PC-facing endpoint and owns one Function Block.
//
//   slot 0 -> groups 0..3   (FB 0)
//   slot 1 -> groups 4..7   (FB 1)
//   slot 2 -> groups 8..11  (FB 2)
//   slot 3 -> groups 12..15 (FB 3)
//
// kNumSlots equals midi2::Host::MAX_DEVICES (4); kGroupsPerSlot is the
// USB-side group count (16) divided by kNumSlots.
constexpr uint8_t kNumSlots       = midi2::Host::MAX_DEVICES;
constexpr uint8_t kGroupsPerSlot  = 4;

// Boots both PHYs (UTMI host first, then INT device with the
// LP_SYS.usb_ctrl swap), spawns the TinyUSB host + device tasks, and
// wires the platform hooks into the supplied m2 objects. Must be
// called once from app_main(), before any midi.* / ci.* / host.*
// calls.
void init(midi2::m2device& midi, midi2::m2ci& ci, midi2::m2host& host);

// Drains both stacks: pumps device-side RX into midi.feedRx, host-side
// RX into host.feedRx (with raw UMP forwarding to the PC side, group
// rewritten into the slot's window), refreshes mounted/alt state on
// the device side, and lets midi.task / host.task run their
// housekeeping. Call every iteration of the main loop.
void task(midi2::m2device& midi, midi2::m2host& host);

// ------------------------------------------------------------------
// Slot management, called by main.cpp from m2host lifecycle events.
// All three functions are no-ops when idx >= kNumSlots.
// ------------------------------------------------------------------

// Marks a slot active/inactive. `alt` is the upstream MIDIStreaming
// alt setting (0 = MIDI 1.0 byte stream, 1 = UMP). Pushes a fresh FB
// Info Notification reflecting the new state.
void slot_set_active(uint8_t idx, bool active, uint8_t alt);

// Sets the FB Name for a slot from the upstream Endpoint Name (or any
// app-provided string). Stored locally and pushed via FB Name
// Notification on each call.
void slot_set_name(uint8_t idx, const char* name);

// Pushes the FB Info + FB Name for a slot in response to a host-side
// Function Block Discovery request. `filter` is the Discovery message
// filter byte (bit 0 = info, bit 1 = name).
void push_slot_advertisement(uint8_t idx, uint8_t filter);

}  // namespace esp32_p4_devkit_bridge

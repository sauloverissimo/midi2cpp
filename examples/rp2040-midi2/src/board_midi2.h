/*
 * board_midi2.h: generic board core (TinyUSB <-> midi2cpp glue).
 *
 * The application layer (player, bridge, etc.) consumes this header and
 * never touches tud_*, pico_*, or any USB symbol directly. After init,
 * the m2device + m2ci instances are wired to the platform USB stack
 * through midi2cpp's five public hooks (setWriteFn, feedRx, setNowFn,
 * setMounted, CI::setRngFn). The app then registers callbacks, sends
 * UMPs, and calls task() in the main loop.
 *
 * Replicating this pattern for another board is a matter of writing
 * <board>_midi2.{h,cpp} that exposes the same two-function surface.
 */
#pragma once

#include "midi2cpp.h"

namespace midi2_board {

// Device identity, declared once and used by both paths that carry it:
// MIDI-CI Discovery (application) and the UMP Stream Device Identity
// Notification (board glue). Keeping one source avoids the two answers
// drifting apart.
inline constexpr uint8_t  kIdentityMfrBytes[3] = {0x7D, 0x00, 0x00};
inline constexpr uint32_t kIdentityMfr         = 0x7D0000;  // first of 3 bytes
inline constexpr uint16_t kIdentityFamily      = 0x0001;
inline constexpr uint16_t kIdentityModel       = 0x0004;
inline constexpr uint32_t kIdentityRevision    = 0x00000400;

// Boots board_init + tusb_init, sets up USB MIDI 2.0 device class, and
// wires the five midi2cpp platform hooks into the supplied m2device /
// m2ci. After this returns, the app can register callbacks, send UMPs,
// and call task() in its main loop.
//
// Must be called once at startup, before any midi.send* / ci.* calls.
void init(midi2::m2device& midi, midi2::m2ci& ci);

// Drains the USB stack (tud_task) and pumps any received UMP words into
// midi.feedRx. Call every iteration of the main loop.
void task(midi2::m2device& midi);

}  // namespace midi2_board

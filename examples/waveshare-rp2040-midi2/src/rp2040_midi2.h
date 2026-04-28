/*
 * rp2040_midi2.h: public API of the rp2040-midi2 board core.
 *
 * The application layer (player, bridge, etc.) consumes this header and
 * never touches tud_*, pico_*, or any USB symbol directly. After init,
 * the m2device + m2ci instances are wired to the platform USB stack
 * through midi2_cpp's five public hooks (setWriteFn, feedRx, setNowFn,
 * setMounted, CI::setRngFn). The app then registers callbacks, sends
 * UMPs, and calls task() in the main loop.
 *
 * Replicating this pattern for another board is a matter of writing
 * <board>_midi2.{h,cpp} that exposes the same two-function surface.
 */
#pragma once

#include "midi2_cpp.h"

namespace rp2040_midi2 {

// Boots board_init + tusb_init, sets up USB MIDI 2.0 device class, and
// wires the five midi2_cpp platform hooks into the supplied m2device /
// m2ci. After this returns, the app can register callbacks, send UMPs,
// and call task() in its main loop.
//
// Must be called once at startup, before any midi.send* / ci.* calls.
void init(midi2::m2device& midi, midi2::m2ci& ci);

// Drains the USB stack (tud_task) and pumps any received UMP words into
// midi.feedRx. Call every iteration of the main loop.
void task(midi2::m2device& midi);

}  // namespace rp2040_midi2

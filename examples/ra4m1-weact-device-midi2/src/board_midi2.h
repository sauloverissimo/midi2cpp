/*
 * board_midi2.h, public API of the ra4m1-weact-device-midi2 board
 * core.
 *
 * The application layer (main.cpp showcase) consumes this header and
 * never touches tud_*, board_*, or any USB symbol directly. After
 * init, the m2device + m2ci instances are wired to TinyUSB through
 * midi2cpp's public hooks (setWriteFn, feedRx, setNowFn, setMounted,
 * setAltSetting, CI::setRngFn). The app then registers callbacks,
 * sends UMPs, and calls task() in the main loop.
 */
#pragma once

#include "midi2cpp.h"

namespace midi2_board {

// Boots board (clocks, USB pins, LED), installs TinyUSB device stack
// with the MIDI 2.0 class driver, enables the RA4M1 on-chip USB LDO
// regulator, and wires the public hooks into the supplied m2device /
// m2ci.
//
// Must be called once from main(), before any midi.send* / ci.* calls.
void init(midi2::m2device& midi, midi2::m2ci& ci);

// Pumps TinyUSB events, drains UMP RX into midi.feedRx, refreshes
// mounted/alt state, and runs library housekeeping (heartbeat,
// deferred sends). Call every iteration of the main loop.
void task(midi2::m2device& midi);

// On-board blue user LED (P0.12, active high) on the WeAct board. The
// weact_ra4m1 board maps LED1 to P0.12 so board_led_write drives the
// real LED. The showcase toggles it on every NoteOn so the LED blinks
// in time with the notes; it is cleared when USB unmounts.
void led(bool on);

}  // namespace midi2_board

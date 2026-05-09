/*
 * xiao_samd21_midi2.h, public API of the xiao-samd21-midi2 board core.
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

namespace xiao_samd21_midi2 {

// Boots board (clocks, USB pins, LED), installs TinyUSB device stack
// with the MIDI 2.0 class driver from PR #3571, and wires the public
// hooks into the supplied m2device / m2ci.
//
// Must be called once from main(), before any midi.send* / ci.* calls.
void init(midi2::m2device& midi, midi2::m2ci& ci);

// Pumps TinyUSB events, drains UMP RX into midi.feedRx, refreshes
// mounted/alt state, and runs library housekeeping (heartbeat,
// deferred sends). Call every iteration of the main loop.
void task(midi2::m2device& midi);

// On-board yellow LED (PA17). Lit while USB is mounted, off otherwise.
void led_show_mounted(bool mounted);

}  // namespace xiao_samd21_midi2

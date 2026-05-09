/*
 * t_display_s3_midi2.h, public API of the t-display-s3-midi2 board core.
 *
 * The application layer consumes this header and never touches tud_*,
 * esp_*, or any USB symbol directly. After init, the m2device + m2ci
 * instances are wired to the platform USB stack through midi2cpp's
 * five public hooks (setWriteFn, feedRx, setNowFn, setMounted,
 * CI::setRngFn). The app then registers callbacks (onNoteOn/Off, onCC,
 * onPitchBend, onPerNotePitchBend), runs the receiver loop, and calls
 * task() in the FreeRTOS loop.
 *
 * The on-board ST7789 1.9" 320x170 display is initialised here via
 * the piano_display component; the showcase observes inbound UMP and
 * drives the piano roll via piano_display::set_note_active().
 */
#pragma once

#include "midi2cpp.h"

namespace t_display_s3_midi2 {

// Boots USB-OTG PHY, installs TinyUSB device task with the MIDI 2.0
// class driver from PR #3571, brings up the ST7789 display + piano UI,
// and wires the five midi2cpp platform hooks into the supplied
// m2device / m2ci. After this returns, the app can register callbacks
// and call task() in its main loop.
//
// Must be called once from app_main(), before any midi.send* / ci.*
// calls.
void init(midi2::m2device& midi, midi2::m2ci& ci);

// Drains the USB stack (tud_task is run on its own FreeRTOS task by
// TinyUSB; this helper drains UMP RX into midi.feedRx and refreshes
// mounted/alt state). Call every iteration of the main loop.
void task(midi2::m2device& midi);

// Shows USB lifecycle on the on-board display info bar (mounted /
// not mounted state, alt setting). Optional, callable from the main
// loop on state-change events.
void show_mounted(bool mounted);

}  // namespace t_display_s3_midi2

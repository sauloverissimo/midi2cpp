/*
 * piano_display.h, public API for the on-board ST7789 piano roll on the
 * LilyGo T-Display S3.
 *
 * The recipe's main.cpp wires the midi2_cpp callbacks (onNoteOn/Off)
 * into set_note_active(); the recipe's board glue spins a FreeRTOS
 * task that calls render_frame() at ~60 fps.
 *
 * Thread safety: set_note_active() and bump_counter() are safe to call
 * from any task, including the TinyUSB task that delivers UMP into
 * midi2_cpp. The internal active-note buffer is a plain `bool[128]`
 * with atomic byte writes; there is no read-modify-write across the
 * boundary, so no lock is required.
 */
#pragma once

#include <cstdint>

namespace piano_display {

enum class Counter : uint8_t {
    NoteOn,
    NoteOff,
    CC,
    PitchBend,
    Other,
};

// Initialise the LovyanGFX driver (Bus_Parallel8 on GPIO39..48 + GP6
// CS, GP7 RS/DC, GP5 RST, GP8 WR, GP9 RD, GP38 BL), allocate the
// full-screen sprite (320x170 16bpp = ~108 KB; lives in PSRAM if
// enabled), and clear the active-note buffer.
void init();

// Mark a MIDI note (0..127) as currently active or released. The piano
// auto-shifts octave to bring the note into view if it would fall
// outside the current 25-key window.
void set_note_active(uint8_t note, bool active);

// Increment one of the per-category UMP counters shown in the info bar.
void bump_counter(Counter counter);

// Update the status string in the info bar (e.g. "host connected").
// Set to a short string (under ~32 chars) to fit the available width.
void set_status(const char* text);

// Render one full frame to the display. Call from a dedicated task at
// the desired refresh rate; ~60 fps is comfortable.
void render_frame();

}  // namespace piano_display

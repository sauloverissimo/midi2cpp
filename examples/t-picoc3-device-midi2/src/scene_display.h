/*
 * scene_display.h, public API for the Scene Cinema visualiser on the
 * LilyGO T-PicoC3 on-board ST7789V (240 x 135, IPS, landscape).
 *
 * The recipe's main.cpp drives the showcase cycle (10 scenes A..J on
 * MT 0x0/0x4/0x5/0xD/0xF). Each scene calls set_scene() at the top and
 * pushes scene-specific events through the notify_*() entry points; a
 * dedicated FreeRTOS-free render loop on core1 paints frames at ~30 fps.
 *
 * Layout (240 x 135):
 *   +-------------------------------------+   24 px header
 *   |  MIDI 2.0   |  Scene C              |
 *   +-------------------------------------+
 *   |                                     |   86 px main canvas
 *   |    ... scene-specific drawing ...   |   (per-scene renderer)
 *   |                                     |
 *   +-------------------------------------+
 *   |  MT 0x4   |  cycle 73%   |  #142    |   25 px footer
 *   +-------------------------------------+
 *
 * Thread safety: notify_*() and set_*() are safe to call from any core.
 * Snapshot state lives in std::atomic-aligned fields and is sampled once
 * per frame by core1.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace scene_display {

enum class Scene : uint8_t {
    None = 0,
    A_FlexData,
    B_PerNote,
    C_Resolution,
    D_ProgramBank,
    E_RpnNrpn,
    F_NoteAttribute,
    G_SysEx8,
    H_DeltaClockstamp,
    I_PENotify,
    J_EndOfClip,
};

// Bring up the ST7789V panel + backlight + framebuffer sprite and launch
// the render task on core1. Must be called after stdio_init_all() and
// after PWR_ON (GP22) has been driven HIGH.
void init();

// Mark the current cycle scene. cycle_count starts at 1.
void set_scene(Scene s, uint32_t cycle_count);

// 0.0 ... 1.0 progress within the current 22 s cycle. Updated every loop.
void set_progress(float fraction);

// USB mount state. Drives the "device mounted" indicator in the header.
void set_mounted(bool mounted);

// Generic UMP counter bump (footer cycles through note / cc / pb / other).
enum class CounterKind : uint8_t { NoteOn, NoteOff, CC, PitchBend, Other };
void bump_counter(CounterKind k);

// Per-scene events the renderers consume.

// Scene A: chord + tempo info.
void notify_flex(uint16_t bpm_x100, uint8_t time_sig_num, uint8_t time_sig_denom,
                 const char* chord);

// Scene B: live Per-Note PB phase (0.0 = no bend, +/-1.0 = full swing).
void notify_per_note(uint8_t note, float pb_phase_signed);

// Scene C: chromatic walk step (note + 32-bit cc + 32-bit pb).
void notify_resolution(uint8_t note, uint16_t vel16, uint32_t cc32, uint32_t pb32);

// Scene D: Program + Bank in a single UMP.
void notify_program(uint8_t program, uint8_t bank_msb, uint8_t bank_lsb);

// Scene E: 4 sub-events. variant selects RPN / NRPN / RelRPN / RelNRPN.
enum class RpnVariant : uint8_t { Rpn, Nrpn, RelRpn, RelNrpn };
void notify_rpn(RpnVariant v, uint8_t msb, uint8_t lsb, int64_t value_or_delta);

// Scene F: pitch_7_9 microtonal attribute.
void notify_attribute(uint8_t note, int16_t cents);

// Scene G: 16 raw 8-bit bytes scrolling.
void notify_sysex8(const uint8_t* bytes, size_t count);

// Scene H: ticks per quarter + delta ticks.
void notify_dctpq(uint16_t tpq, uint32_t delta_ticks);

// Scene I: PE Notify event (property name + subscriber count).
void notify_pe(const char* property, uint8_t subscribers);

// Scene J: end-of-clip marker (renderer fades out).
void notify_end_of_clip();

}  // namespace scene_display

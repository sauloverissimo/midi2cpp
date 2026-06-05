#ifndef GABARITO_BATTERY_H
#define GABARITO_BATTERY_H

/*
 * gabarito_battery.h - the midi2flow interpretation answer key.
 *
 * One source of truth for the *interpretation* gabarito: a paced, musical,
 * note-paired battery where every gesture has a KNOWN expected reading. It is
 * shared verbatim by two consumers, so what the device plays is exactly what
 * the check verifies:
 *
 *   - the device firmware (GABARITO_MODE=INTERP) plays each challenge live;
 *   - the offline replay check feeds the same schedule through GingoFlow +
 *     gingoduino and grades the interpretation, no hardware needed.
 *
 * This is the opposite of the capture gabarito (the seq flood): that one is an
 * adversarial throughput test with intentionally unpaired notes; this one is a
 * clean musical reference for noteIndex / chordIndex / naming.
 *
 * Header-only, no allocation, includes only <cstdint>. The timing model lives
 * in schedule(): it turns a Challenge into time-ordered (tMs, note, isOn)
 * events; each consumer decides what "advance to tMs" means (the device holds
 * USB, the replay stamps the ingest timestamp).
 */

#include <cstdint>

namespace gab {

enum Gesture {
    CHORD,            // notes struck together (stepMs apart = rolled), released together
    ARP,              // notes one at a time, each released before the next (a melody)
    HANG_THEN_CHORD,  // notes[0] pressed and NEVER released, then notes[1..] as a chord
};

struct Challenge {
    const char* name;      // short id, also the host marker label
    const char* expected;  // the answer key, human readable (judged in the check)
    Gesture     gesture;
    uint8_t     notes[12];
    uint8_t     count;
    uint16_t    holdMs;    // CHORD: hold before release; ARP: per-note hold
    uint16_t    stepMs;    // CHORD: onset spread (0 = simultaneous); ARP: onset spacing
    uint16_t    gapMs;     // silence after the gesture, so the flow closes the group
};

// The battery. Paced (~holds + gaps) so the host can report each gesture without
// the UART stalling PIO-USB, and so the held set empties between challenges.
static const Challenge kBattery[] = {
    // --- durations: noteIndex pairing + durationMs ---
    {"dur-short", "single C4, duration ~120ms",  CHORD, {60},             1,  120, 0, 200},
    {"dur-long",  "single C4, duration ~1500ms", CHORD, {60},             1, 1500, 0, 300},

    // --- triad qualities: chordIndex grouping + gingoduino naming ---
    {"maj",  "C major",      CHORD, {60, 64, 67}, 3, 400, 0, 250},
    {"min",  "C minor",      CHORD, {60, 63, 67}, 3, 400, 0, 250},
    {"dim",  "C diminished", CHORD, {60, 63, 66}, 3, 400, 0, 250},
    {"aug",  "C augmented",  CHORD, {60, 64, 68}, 3, 400, 0, 250},
    {"sus4", "C sus4",       CHORD, {60, 65, 67}, 3, 400, 0, 250},

    // --- sevenths ---
    {"maj7", "C major 7",                CHORD, {60, 64, 67, 71}, 4, 400, 0, 250},
    {"dom7", "C dominant 7",             CHORD, {60, 64, 67, 70}, 4, 400, 0, 250},
    {"min7", "C minor 7",                CHORD, {60, 63, 67, 70}, 4, 400, 0, 250},
    {"dim7", "C diminished 7",           CHORD, {60, 63, 66, 69}, 4, 400, 0, 250},
    {"m7b5", "C half-diminished (m7b5)", CHORD, {60, 63, 66, 70}, 4, 400, 0, 250},

    // --- inversions: same chord, bass moved (naming must hold) ---
    {"inv1", "C major / 1st inversion (E in the bass)", CHORD, {64, 67, 72}, 3, 400, 0, 250},
    {"inv2", "C major / 2nd inversion (G in the bass)", CHORD, {67, 72, 76}, 3, 400, 0, 250},

    // --- voicing: open spacing (naming must hold) ---
    {"open", "C major, open voicing", CHORD, {60, 67, 76}, 3, 400, 0, 250},

    // --- onset spread: rolled chord must still group as ONE chord ---
    {"spread", "C major rolled (25ms onset spread) -> one chord", CHORD, {60, 64, 67}, 3, 400, 25, 250},

    // --- the hard one: same notes as a chord, but released between -> a melody ---
    {"arp", "C-E-G arpeggio -> 3 separate notes (melody), NOT a chord", ARP, {60, 64, 67}, 3, 120, 200, 250},

    // --- progression I-IV-V-I in C (field / function fodder) ---
    {"prog-I",  "C major (I)",  CHORD, {60, 64, 67}, 3, 350, 0, 200},
    {"prog-IV", "F major (IV)", CHORD, {65, 69, 72}, 3, 350, 0, 200},
    {"prog-V",  "G major (V)",  CHORD, {67, 71, 74}, 3, 350, 0, 200},
    {"prog-I2", "C major (I)",  CHORD, {60, 64, 67}, 3, 350, 0, 300},

    // --- robustness: a hung note (no note-off) then a chord ---
    {"hang", "hung D4 + C major -> grouping must survive the stuck note",
     HANG_THEN_CHORD, {62, 60, 64, 67}, 4, 400, 0, 300},
};

static const unsigned kBatteryCount = sizeof(kBattery) / sizeof(kBattery[0]);

// Emit the timed note events of one challenge, in non-decreasing tMs order, to a
// sink callable as sink(uint16_t tMs, uint8_t note, bool isOn). tMs is relative
// to the start of the challenge. Returns the gesture span in ms (excludes gapMs).
template <class Sink>
inline uint16_t schedule(const Challenge& c, Sink&& sink) {
    uint16_t end = 0;
    switch (c.gesture) {
        case CHORD: {
            // staggered onsets (stepMs; 0 = simultaneous), common release.
            for (uint8_t i = 0; i < c.count; ++i)
                sink((uint16_t)(i * c.stepMs), c.notes[i], true);
            const uint16_t rel = (uint16_t)(c.holdMs + (c.count - 1) * c.stepMs);
            for (uint8_t i = 0; i < c.count; ++i)
                sink(rel, c.notes[i], false);
            end = rel;
        } break;

        case ARP: {
            uint16_t t = 0;
            for (uint8_t i = 0; i < c.count; ++i) {
                sink(t, c.notes[i], true);
                sink((uint16_t)(t + c.holdMs), c.notes[i], false);  // released before next
                t = (uint16_t)(t + c.stepMs);
            }
            end = (uint16_t)((c.count - 1) * c.stepMs + c.holdMs);
        } break;

        case HANG_THEN_CHORD: {
            sink(0, c.notes[0], true);  // hung note: pressed, never released
            for (uint8_t i = 1; i < c.count; ++i)
                sink(c.holdMs, c.notes[i], true);
            const uint16_t rel = (uint16_t)(c.holdMs * 2);
            for (uint8_t i = 1; i < c.count; ++i)
                sink(rel, c.notes[i], false);
            end = rel;
        } break;
    }
    return end;
}

}  // namespace gab

#endif  // GABARITO_BATTERY_H

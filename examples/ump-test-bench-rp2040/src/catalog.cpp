/*
 * catalog.cpp, scaffold of the deterministic UMP catalog.
 *
 * Phase: SCAFFOLD. Three reference entries are implemented end to end
 * to establish the pattern (idx 1 Set Tempo BPM=120, idx 3 Set Time
 * Signature 4/4, idx 14 Set Chord Name CMaj7); the remaining 98 entries
 * print a stub EMIT line and are filled in sequentially during the
 * next phase. The reference entries are the same three Microsoft uses
 * as cross-checks against the Windows MIDI Services consumer (Appendix
 * A of the spec doc).
 *
 * EMIT log format (matches §8 of the spec doc):
 *
 *   EMIT idx=##  label=<descriptor>  words=W0 W1 W2 W3
 *
 * Words are 8 hex digits uppercase, space-separated; entries that emit
 * fewer than 4 words pad the trailing slots with 00000000 so the line
 * width stays constant. Multi-packet entries print one EMIT line per
 * UMP packet, each tagged with the same idx so they can be correlated
 * back to a single catalog row.
 */
#include "catalog.h"

#include <cstdio>

#include "pico/stdlib.h"
#include "pico/time.h"

namespace ump_test_bench {

namespace {

void log_words4(uint8_t idx, const char* label, const uint32_t w[4]) {
    std::printf("EMIT idx=%02u  label=%-40s  words=%08X %08X %08X %08X\r\n",
                (unsigned)idx, label,
                (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3]);
}

void log_stub(uint8_t idx) {
    std::printf("EMIT idx=%02u  label=%-40s  words=%08X %08X %08X %08X\r\n",
                (unsigned)idx, "TODO not yet implemented",
                0u, 0u, 0u, 0u);
}

/*--------------------------------------------------------------------+
 * Reference entries (3 of 101). Each function emits its UMPs and logs
 * the wire bytes via log_words4. The midi2 C99 helpers in midi2.h are
 * used to capture exact word output for the EMIT line; the parallel
 * sendXxx call into m2device is what actually goes on the USB bus.
 *--------------------------------------------------------------------*/

// idx 1, §5.1, Set Tempo BPM=120 -> tenNsPerQuarter = 50_000_000.
void emit_idx_01(midi2::m2device& midi) {
    constexpr uint8_t  kGroup = 0;
    constexpr uint32_t kTenNs = 50000000u;

    midi.sendTempo(kGroup, kTenNs);

    uint32_t w[4] = {0, 0, 0, 0};
    midi2_msg_set_tempo(w, kGroup, /*address*/ 0x01, /*channel*/ 0, kTenNs);
    log_words4(1, "set_tempo BPM=120", w);
}

// idx 3, §5.1, Set Time Signature 4/4 with 8 thirty-seconds per quarter.
// The midi2_cpp wrapper takes the denominator in raw musical form
// (4 for 4/4); midi2_msg_set_time_signature handles the encoding per
// M2-104 §7.5.4 internally.
void emit_idx_03(midi2::m2device& midi) {
    constexpr uint8_t kGroup = 0;
    constexpr uint8_t kNum   = 4;
    constexpr uint8_t kDenom = 4;

    midi.sendTimeSignature(kGroup, kNum, kDenom);

    uint32_t w[4] = {0, 0, 0, 0};
    midi2_msg_set_time_signature(w, kGroup, /*address*/ 0x01, /*channel*/ 0,
                                 kNum, kDenom, /*thirty_seconds_per_qn*/ 8);
    log_words4(3, "set_time_signature 4/4 8/32", w);
}

// idx 14, §5.1, Set Chord Name CMaj7. Group-level address, no
// alterations, no bass override.
void emit_idx_14(midi2::m2device& midi) {
    midi2::ChordDescriptor c{};
    c.address        = 0x01;  // group-level
    c.channel        = 0;
    c.tonicSharpFlat = 0;     // natural
    c.tonicNote      = 3;     // C, per M2-104 Table 15
    c.chordType      = 0x03;  // Major 7th, per M2-104 Table 14
    midi.sendChordName(/*group*/ 0, c);

    uint32_t w[4] = {0, 0, 0, 0};
    midi2_msg_set_chord_name(w, /*group*/ 0, /*address*/ 0x01, /*channel*/ 0,
                             c.tonicSharpFlat, c.tonicNote, c.chordType,
                             c.alt1Type, c.alt1Degree,
                             c.alt2Type, c.alt2Degree,
                             c.alt3Type, c.alt3Degree,
                             c.alt4Type, c.alt4Degree,
                             c.bassSharpFlat, c.bassNote, c.bassChordType,
                             c.bassAlt1Type, c.bassAlt1Degree,
                             c.bassAlt2Type, c.bassAlt2Degree);
    log_words4(14, "set_chord_name CMaj7", w);
}

}  // namespace

bool catalogEmit(uint8_t idx, midi2::m2device& midi) {
    if (idx >= kCatalogSize) {
        std::printf("[catalog] idx=%u out of range (max %u)\r\n",
                    (unsigned)idx, (unsigned)(kCatalogSize - 1));
        return false;
    }

    switch (idx) {
        case 1:  emit_idx_01(midi); break;
        case 3:  emit_idx_03(midi); break;
        case 14: emit_idx_14(midi); break;
        default: log_stub(idx);     break;
    }
    return true;
}

void catalogEmitAll(midi2::m2device& midi, uint32_t tx_pause_ms) {
    for (uint8_t i = 0; i < kCatalogSize; ++i) {
        catalogEmit(i, midi);
        if (tx_pause_ms > 0) sleep_ms(tx_pause_ms);
    }
}

}  // namespace ump_test_bench

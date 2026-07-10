/*
 * catalog.cpp, deterministic UMP catalog (101 entries).
 *
 * Each entry follows the same shape:
 *   1. issue the midi.sendXxx() call (or midi2_board::pumpRaw for the
 *      five entries that have no midi2cpp sender: Endpoint Discovery,
 *      Stream Config Request, FB Discovery, plus the two intentional
 *      edge cases),
 *   2. capture the same wire bytes via the corresponding midi2 C99
 *      midi2_msg_xxx builder, padded to 4 words for log alignment,
 *   3. emit one EMIT line per UMP packet (matching what reaches the
 *      USB bus byte-for-byte).
 *
 * EMIT line format (matches §8 of the spec doc):
 *
 *   EMIT idx=##  label=<descriptor>  words=W0 W1 W2 W3
 *
 * Words are 8 hex digits uppercase, space-separated; entries that emit
 * fewer than 4 real words pad the trailing slots with 00000000 so the
 * line width stays constant. Multi-packet entries print one EMIT line
 * per UMP packet, each tagged with the same idx.
 *
 * The chord_type codes used in the bench (0x01 Major, 0x03 Major 7,
 * 0x07 Minor) are the values the midi2 C99 inline doc records. Other
 * chord types in §5.1 (Dm7, G7, F#dim, CMaj7/E) are best-effort with
 * the closest available code plus alterations; M2-104 Table 14 should
 * be consulted on the host side to verify.
 */
#include "catalog.h"
#include "board_midi2.h"

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "pico/time.h"

namespace ump_test_bench {

namespace {

constexpr uint8_t kCatalogGroup   = 0;
constexpr uint8_t kCatalogChannel = 0;

void log_words(uint8_t idx, const char* label, const uint32_t* w, uint8_t n_words) {
    uint32_t pad[4] = {0, 0, 0, 0};
    if (n_words > 4) n_words = 4;
    for (uint8_t i = 0; i < n_words; ++i) pad[i] = w[i];
    std::printf("EMIT idx=%02u  label=%-44s  words=%08X %08X %08X %08X\r\n",
                (unsigned)idx, label,
                (unsigned)pad[0], (unsigned)pad[1], (unsigned)pad[2], (unsigned)pad[3]);
}

void log_word1(uint8_t idx, const char* label, uint32_t w0) {
    std::printf("EMIT idx=%02u  label=%-44s  words=%08X 00000000 00000000 00000000\r\n",
                (unsigned)idx, label, (unsigned)w0);
}

uint32_t bpm_to_ten_ns(uint32_t bpm) {
    return 6000000000u / bpm;
}

// M2-104-UM §7.5.4 encodes the time-signature denominator as a
// negative power-of-2 exponent: 4/4 ships exp=2 (since 2^2=4), 6/8
// ships exp=3, etc. The midi2 C99 builder midi2_msg_time_sig writes
// the second argument straight into word1 bits 16-23 with no
// conversion, so we have to feed it the exponent ourselves. Same trick
// applies to midi.sendTimeSignature, which calls the same builder.
constexpr uint8_t denom_to_exp(uint8_t denom_human) {
    switch (denom_human) {
        case 1:   return 0;
        case 2:   return 1;
        case 4:   return 2;
        case 8:   return 3;
        case 16:  return 4;
        case 32:  return 5;
        case 64:  return 6;
        case 128: return 7;
        default:  return 0;
    }
}

/*--------------------------------------------------------------------+
 * §5.1 Flex Data bank 0x00 (Setup and Performance), idx 0..18
 *--------------------------------------------------------------------*/

void emit_set_tempo(uint8_t idx, midi2::m2device& midi, uint32_t bpm, const char* label) {
    const uint32_t ten_ns = bpm_to_ten_ns(bpm);
    midi.sendTempo(kCatalogGroup, ten_ns);
    uint32_t w[4]; midi2_msg_tempo(w, kCatalogGroup, ten_ns);
    log_words(idx, label, w, 4);
}

void emit_set_time_sig(uint8_t idx, midi2::m2device& midi,
                       uint8_t num, uint8_t denom_human, const char* label) {
    const uint8_t exp = denom_to_exp(denom_human);
    midi.sendTimeSignature(kCatalogGroup, num, exp);
    uint32_t w[4]; midi2_msg_time_sig(w, kCatalogGroup, num, exp, 8);
    log_words(idx, label, w, 4);
}

// Set Key Signature with explicit tonic per M2-104 Tabela 15.
// midi.sendKeySignature drops to midi2_msg_key_sig (the simple builder)
// which always writes tonic=0 (Unknown). We bypass via pumpRaw + the
// _full builder so the tonic field on the wire matches what the host
// is expected to read.
//
// Tonic codes:  0=unknown, 1=A, 2=B, 3=C, 4=D, 5=E, 6=F, 7=G.
// Key type:     0=major, 1=minor, 2=none/atonal, 3=reserved.
void emit_set_key_sig(uint8_t idx, midi2::m2device& /*midi*/,
                      int8_t sf, uint8_t tonic, uint8_t key_type,
                      const char* label) {
    uint32_t w[4];
    midi2_msg_key_sig_full(w, kCatalogGroup, /*address*/ 0x01, /*channel*/ 0,
                            sf, tonic, key_type);
    midi2_board::pumpRaw(w, 4);
    log_words(idx, label, w, 4);
}

void emit_set_chord(uint8_t idx, midi2::m2device& midi,
                    const midi2::ChordDescriptor& c, const char* label) {
    midi.sendChordName(kCatalogGroup, c);
    uint32_t w[4];
    midi2_msg_chord_name(w, kCatalogGroup, c.address, c.channel,
                          c.tonicSharpFlat, c.tonicNote, c.chordType,
                          c.alt1Type, c.alt1Degree,
                          c.alt2Type, c.alt2Degree,
                          c.alt3Type, c.alt3Degree,
                          c.alt4Type, c.alt4Degree,
                          c.bassSharpFlat, c.bassNote, c.bassChordType,
                          c.bassAlt1Type, c.bassAlt1Degree,
                          c.bassAlt2Type, c.bassAlt2Degree);
    log_words(idx, label, w, 4);
}

midi2::ChordDescriptor make_chord(uint8_t tonicNote, uint8_t chordType,
                                   uint8_t bassNote = 0, uint8_t bassType = 0) {
    midi2::ChordDescriptor c{};
    c.address        = 0x01;  // group-level
    c.channel        = 0;
    c.tonicSharpFlat = 0;
    c.tonicNote      = tonicNote;
    c.chordType      = chordType;
    c.bassNote       = bassNote;
    c.bassChordType  = bassType;
    return c;
}

/*--------------------------------------------------------------------+
 * §5.2 / §5.3 Flex Data Text helpers
 *--------------------------------------------------------------------*/

constexpr uint8_t kFlexBankMetadata = 0x01;
constexpr uint8_t kFlexBankPerfText = 0x02;

// Metadata Text (bank 0x01) status codes per midi2 C99 enum names below.
constexpr uint8_t kFlexProjectName       = 0x01;
constexpr uint8_t kFlexCompositionName   = 0x02;
constexpr uint8_t kFlexMidiClipName      = 0x03;
constexpr uint8_t kFlexCopyright         = 0x04;
constexpr uint8_t kFlexComposer          = 0x05;
constexpr uint8_t kFlexLyricist          = 0x06;
constexpr uint8_t kFlexArranger          = 0x07;
constexpr uint8_t kFlexPublisher         = 0x08;
constexpr uint8_t kFlexPrimaryPerformer  = 0x09;

// Performance Text (bank 0x02) status codes.
constexpr uint8_t kFlexLyrics         = 0x01;
constexpr uint8_t kFlexLyricsLanguage = 0x02;
constexpr uint8_t kFlexRuby           = 0x03;
constexpr uint8_t kFlexComment        = 0x05;

void emit_flex_text(uint8_t idx, midi2::m2device& midi,
                    uint8_t bank, uint8_t status,
                    const char* text, const char* label) {
    midi.sendFlexText(kCatalogGroup, bank, status, text);
    // Capture: replay the same text through the C99 builder, fragmenting
    // the same way the wrapper does (12 bytes per UMP, format 0/1/2/3).
    const uint8_t  group   = kCatalogGroup;
    const uint8_t  address = 0x01;     // group-level
    const uint8_t  channel = 0;
    const uint16_t total   = (uint16_t)std::strlen(text);
    const uint8_t* bytes   = (const uint8_t*)text;
    if (total <= 12) {
        uint32_t w[4];
        midi2_msg_flex_text(w, group, /*format*/ 0, address, channel,
                             bank, status, bytes, (uint8_t)total);
        log_words(idx, label, w, 4);
        return;
    }
    uint16_t off = 0;
    while (off < total) {
        uint8_t  chunk = (uint8_t)((total - off) > 12 ? 12 : (total - off));
        uint8_t  format;
        if (off == 0)                         format = 1;  // start
        else if ((off + chunk) >= total)      format = 3;  // end
        else                                  format = 2;  // continue
        uint32_t w[4];
        midi2_msg_flex_text(w, group, format, address, channel,
                             bank, status, bytes + off, chunk);
        log_words(idx, label, w, 4);
        off = (uint16_t)(off + chunk);
    }
}

/*--------------------------------------------------------------------+
 * §5.4 / §5.5 Channel Voice helpers
 *--------------------------------------------------------------------*/

void emit_note_on2(uint8_t idx, midi2::m2device& midi,
                   uint8_t note, uint16_t vel, const char* label) {
    midi.sendNoteOn(kCatalogGroup, kCatalogChannel, note, vel);
    uint32_t w[4]; midi2_msg_note_on(w, kCatalogGroup, kCatalogChannel, note, vel, 0, 0);
    log_words(idx, label, w, 2);
}

void emit_note_off2(uint8_t idx, midi2::m2device& midi,
                    uint8_t note, uint16_t vel, const char* label) {
    midi.sendNoteOff(kCatalogGroup, kCatalogChannel, note, vel);
    uint32_t w[4]; midi2_msg_note_off(w, kCatalogGroup, kCatalogChannel, note, vel, 0, 0);
    log_words(idx, label, w, 2);
}

void emit_cc2(uint8_t idx, midi2::m2device& midi,
              uint8_t cc, uint32_t val, const char* label) {
    midi.sendCC(kCatalogGroup, kCatalogChannel, cc, val);
    uint32_t w[4]; midi2_msg_cc(w, kCatalogGroup, kCatalogChannel, cc, val);
    log_words(idx, label, w, 2);
}

void emit_pitchbend2(uint8_t idx, midi2::m2device& midi,
                     uint32_t val, const char* label) {
    midi.sendPitchBend(kCatalogGroup, kCatalogChannel, val);
    uint32_t w[4]; midi2_msg_pitch_bend(w, kCatalogGroup, kCatalogChannel, val);
    log_words(idx, label, w, 2);
}

/*--------------------------------------------------------------------+
 * §5.6 System helpers (1-word messages)
 *--------------------------------------------------------------------*/

void emit_system_clock(uint8_t idx, midi2::m2device& midi, const char* label) {
    midi.sendClock(kCatalogGroup);
    log_word1(idx, label, midi2_msg_system_timing_clock(kCatalogGroup));
}

/*--------------------------------------------------------------------+
 * §5.7 SysEx7 + SysEx8 helpers
 *--------------------------------------------------------------------*/

void emit_sysex7_one(uint8_t idx, midi2::m2device& midi,
                     uint8_t status_nibble_high, const uint8_t* data, uint8_t len,
                     const char* label) {
    // SysEx7 packet status nibble is in the high nibble of the 8-bit status
    // byte; len lives in the low nibble. midi2_msg_sysex7_packet OR's the
    // two together internally.
    uint32_t w[4] = {0, 0, 0, 0};
    midi2_msg_sysex7_packet(w, kCatalogGroup, status_nibble_high, data, len);
    midi2_board::pumpRaw(w, 2);
    log_words(idx, label, w, 2);
}

void emit_sysex8_one(uint8_t idx, midi2::m2device& midi,
                     uint8_t status_nibble_high, uint8_t stream_id,
                     const uint8_t* data, uint8_t len,
                     const char* label) {
    uint32_t w[4] = {0, 0, 0, 0};
    midi2_msg_sysex8_packet(w, kCatalogGroup, status_nibble_high, stream_id, data, len);
    midi2_board::pumpRaw(w, 4);
    log_words(idx, label, w, 4);
}

/*--------------------------------------------------------------------+
 * §5.8 UMP Stream helpers (4-word messages)
 *--------------------------------------------------------------------*/

void emit_endpoint_name_text(uint8_t idx, const char* text, const char* label) {
    const uint8_t* bytes = (const uint8_t*)text;
    uint16_t       total = (uint16_t)std::strlen(text);
    if (total <= 14) {
        uint32_t w[4];
        midi2_msg_stream_endpoint_name(w, /*format*/ 0, bytes, (uint8_t)total);
        midi2_board::pumpRaw(w, 4);
        log_words(idx, label, w, 4);
        return;
    }
    uint16_t off = 0;
    while (off < total) {
        uint8_t  chunk = (uint8_t)((total - off) > 14 ? 14 : (total - off));
        uint8_t  format = (off == 0) ? 1 : ((off + chunk) >= total ? 3 : 2);
        uint32_t w[4];
        midi2_msg_stream_endpoint_name(w, format, bytes + off, chunk);
        midi2_board::pumpRaw(w, 4);
        log_words(idx, label, w, 4);
        off = (uint16_t)(off + chunk);
    }
}

void emit_product_id_text(uint8_t idx, const char* text, const char* label) {
    const uint8_t* bytes = (const uint8_t*)text;
    uint16_t       total = (uint16_t)std::strlen(text);
    if (total <= 14) {
        uint32_t w[4];
        midi2_msg_stream_product_id(w, /*format*/ 0, bytes, (uint8_t)total);
        midi2_board::pumpRaw(w, 4);
        log_words(idx, label, w, 4);
        return;
    }
    uint16_t off = 0;
    while (off < total) {
        uint8_t  chunk = (uint8_t)((total - off) > 14 ? 14 : (total - off));
        uint8_t  format = (off == 0) ? 1 : ((off + chunk) >= total ? 3 : 2);
        uint32_t w[4];
        midi2_msg_stream_product_id(w, format, bytes + off, chunk);
        midi2_board::pumpRaw(w, 4);
        log_words(idx, label, w, 4);
        off = (uint16_t)(off + chunk);
    }
}

void emit_fb_name_text(uint8_t idx, uint8_t fb_num, const char* text, const char* label) {
    const uint8_t* bytes = (const uint8_t*)text;
    uint16_t       total = (uint16_t)std::strlen(text);
    if (total <= 13) {
        uint32_t w[4];
        midi2_msg_stream_fb_name(w, /*format*/ 0, fb_num, bytes, (uint8_t)total);
        midi2_board::pumpRaw(w, 4);
        log_words(idx, label, w, 4);
        return;
    }
    uint16_t off = 0;
    while (off < total) {
        uint8_t  chunk = (uint8_t)((total - off) > 13 ? 13 : (total - off));
        uint8_t  format = (off == 0) ? 1 : ((off + chunk) >= total ? 3 : 2);
        uint32_t w[4];
        midi2_msg_stream_fb_name(w, format, fb_num, bytes + off, chunk);
        midi2_board::pumpRaw(w, 4);
        log_words(idx, label, w, 4);
        off = (uint16_t)(off + chunk);
    }
}

/*--------------------------------------------------------------------+
 * Identity constants used by §5.8 entries (mirror main.cpp).
 *--------------------------------------------------------------------*/
const uint8_t  kBenchMfrId[3]     = {0x7D, 0x00, 0x00};
const uint16_t kBenchFamily       = 0x0001;
const uint16_t kBenchModel        = 0x0001;
const uint32_t kBenchVersion      = 0x00010000;
const char     kBenchEndpoint[]   = "UMP Reference Emitter";
const char     kBenchProductInst[] = "UMPReferenceEmitter-bench-0001";
const char     kBenchFbName[]     = "Test Bench Group 0";

/*--------------------------------------------------------------------+
 * Stub for not-yet-implemented entries.
 *--------------------------------------------------------------------*/
void emit_stub(uint8_t idx) {
    uint32_t z[4] = {0, 0, 0, 0};
    log_words(idx, "TODO not yet implemented", z, 4);
}

}  // namespace

bool catalogEmit(uint8_t idx, midi2::m2device& midi) {
    if (idx >= kCatalogSize) {
        std::printf("[catalog] idx=%u out of range (max %u)\r\n",
                    (unsigned)idx, (unsigned)(kCatalogSize - 1));
        return false;
    }

    switch (idx) {
        /* ============================================================
         * §5.1 Flex Data 0x00 (Setup and Performance)
         * ============================================================ */
        case 0:  emit_set_tempo(0,  midi, 60,  "set_tempo BPM=60");              break;
        case 1:  emit_set_tempo(1,  midi, 120, "set_tempo BPM=120");             break;
        case 2:  emit_set_tempo(2,  midi, 240, "set_tempo BPM=240");             break;
        case 3:  emit_set_time_sig(3,  midi, 4, 4, "set_time_signature 4/4");    break;
        case 4:  emit_set_time_sig(4,  midi, 3, 4, "set_time_signature 3/4");    break;
        case 5:  emit_set_time_sig(5,  midi, 6, 8, "set_time_signature 6/8");    break;
        case 6:  emit_set_time_sig(6,  midi, 5, 4, "set_time_signature 5/4");    break;
        case 7:  emit_set_time_sig(7,  midi, 7, 8, "set_time_signature 7/8");    break;
        case 8: {
            midi.sendMetronome(kCatalogGroup, /*primary*/ 24, 0, 0, 0, 0, 0);
            uint32_t w[4]; midi2_msg_metronome(w, kCatalogGroup, 24, 0, 0, 0, 0, 0);
            log_words(8, "set_metronome primary=24", w, 4);
        } break;
        case 9:  emit_set_key_sig(9,  midi,  0, /*C*/ 3, /*major*/ 0, "set_key_signature C maj");   break;
        case 10: emit_set_key_sig(10, midi,  1, /*G*/ 7, /*major*/ 0, "set_key_signature G maj");   break;
        case 11: emit_set_key_sig(11, midi, -1, /*F*/ 6, /*major*/ 0, "set_key_signature F maj");   break;
        case 12: emit_set_key_sig(12, midi,  2, /*B*/ 2, /*minor*/ 1, "set_key_signature B min");   break;
        case 13: emit_set_key_sig(13, midi,  0, /*unk*/ 0, /*major*/ 0, "set_key_signature unknown"); break;
        case 14: emit_set_chord(14, midi, make_chord(/*C*/3, /*Maj7*/0x03), "set_chord_name CMaj7"); break;
        case 15: emit_set_chord(15, midi, make_chord(/*D*/4, /*Minor*/0x07), "set_chord_name Dm (best-effort 7th)"); break;
        case 16: emit_set_chord(16, midi, make_chord(/*G*/7, /*Major*/0x01), "set_chord_name G (best-effort dom7)");  break;
        case 17: emit_set_chord(17, midi, make_chord(/*F*/6, /*Minor*/0x07), "set_chord_name F# dim (best-effort)");  break;
        case 18: {
            auto c = make_chord(/*C*/3, /*Maj7*/0x03, /*bass E*/5, /*bass type*/0);
            emit_set_chord(18, midi, c, "set_chord_name CMaj7/E (bass override)");
        } break;

        /* ============================================================
         * §5.2 Flex Data 0x01 (Metadata Text)
         * ============================================================ */
        case 19: emit_flex_text(19, midi, kFlexBankMetadata, kFlexProjectName,      "Demo Project",      "metadata project_name");      break;
        case 20: emit_flex_text(20, midi, kFlexBankMetadata, kFlexCompositionName,  "Test Song",         "metadata composition_name");  break;
        case 21: emit_flex_text(21, midi, kFlexBankMetadata, kFlexMidiClipName,     "Clip 1",            "metadata midi_clip_name");    break;
        case 22: emit_flex_text(22, midi, kFlexBankMetadata, kFlexCopyright,        "(c) 2026 Bench",    "metadata copyright");         break;
        case 23: emit_flex_text(23, midi, kFlexBankMetadata, kFlexComposer,         "Test User",         "metadata composer");          break;
        case 24: emit_flex_text(24, midi, kFlexBankMetadata, kFlexLyricist,         "Test User",         "metadata lyricist");          break;
        case 25: emit_flex_text(25, midi, kFlexBankMetadata, kFlexArranger,         "Bench",             "metadata arranger");          break;
        case 26: emit_flex_text(26, midi, kFlexBankMetadata, kFlexPublisher,        "Bench",             "metadata publisher");         break;
        case 27: emit_flex_text(27, midi, kFlexBankMetadata, kFlexPrimaryPerformer, "Bench",             "metadata primary_performer"); break;
        case 28: emit_flex_text(28, midi, kFlexBankMetadata, kFlexProjectName,
                                "Long Project Name That Spans Multiple UMP Packets For Testing",
                                "metadata project_name (multi-packet)"); break;
        case 29: emit_flex_text(29, midi, kFlexBankMetadata, kFlexComposer,
                                "Compositor Nao Existente Com Acentuacao Em UTF8",
                                "metadata composer (multi-packet UTF-8)"); break;

        /* ============================================================
         * §5.3 Flex Data 0x02 (Performance Text)
         * ============================================================ */
        case 30: emit_flex_text(30, midi, kFlexBankPerfText, kFlexLyrics,         "La la la",         "perf lyrics");          break;
        case 31: emit_flex_text(31, midi, kFlexBankPerfText, kFlexLyricsLanguage, "en-US",            "perf lyrics_language"); break;
        case 32: emit_flex_text(32, midi, kFlexBankPerfText, kFlexRuby,           "(test ruby)",      "perf ruby");            break;
        case 33: emit_flex_text(33, midi, kFlexBankPerfText, kFlexLyrics,
                                "Letra de teste com conteudo razoavelmente longo para multipacket",
                                "perf lyrics (multi-packet)"); break;
        case 34: emit_flex_text(34, midi, kFlexBankPerfText, kFlexComment,       "Bench note",       "perf comment");         break;

        /* ============================================================
         * §5.4 MIDI 2.0 Channel Voice (mt 0x4)
         * ============================================================ */
        case 35: emit_note_on2 (35, midi, 60, 0x8000, "note_on n=60 vel=0x8000");      break;
        case 36: emit_note_off2(36, midi, 60, 0x0000, "note_off n=60");                break;
        case 37: emit_note_on2 (37, midi, 64, 0xFFFF, "note_on n=64 vel=0xFFFF max");  break;
        case 38: emit_note_off2(38, midi, 64, 0x0000, "note_off n=64");                break;
        case 39: emit_note_on2 (39, midi, 67, 0x4000, "note_on n=67 vel=0x4000");      break;
        case 40: emit_note_off2(40, midi, 67, 0x0000, "note_off n=67");                break;
        case 41: {
            midi.sendPolyPressure(kCatalogGroup, kCatalogChannel, 60, 0x80000000u);
            uint32_t w[4]; midi2_msg_poly_pressure(w, kCatalogGroup, kCatalogChannel, 60, 0x80000000u);
            log_words(41, "poly_pressure n=60 val=0x80000000", w, 2);
        } break;
        case 42: {
            midi.sendChannelPressure(kCatalogGroup, kCatalogChannel, 0x40000000u);
            uint32_t w[4]; midi2_msg_chan_pressure(w, kCatalogGroup, kCatalogChannel, 0x40000000u);
            log_words(42, "channel_pressure val=0x40000000", w, 2);
        } break;
        case 43: emit_pitchbend2(43, midi, 0x80000000u, "pitch_bend val=0x80000000 center"); break;
        case 44: emit_pitchbend2(44, midi, 0xFFFFFFFFu, "pitch_bend val=0xFFFFFFFF max");    break;
        case 45: emit_pitchbend2(45, midi, 0x00000000u, "pitch_bend val=0x00000000 min");    break;
        case 46: emit_cc2(46, midi,  1, 0x80000000u, "cc cc=1 mod_wheel val=0x80000000"); break;
        case 47: emit_cc2(47, midi,  7, 0xC0000000u, "cc cc=7 volume val=0xC0000000");   break;
        case 48: emit_cc2(48, midi, 64, 0xFFFFFFFFu, "cc cc=64 sustain val=0xFFFFFFFF"); break;
        case 49: {
            midi.sendProgram(kCatalogGroup, kCatalogChannel, /*program*/ 5,
                             /*bank_msb*/ 0, /*bank_lsb*/ 0, /*bank_valid*/ true);
            uint32_t w[4]; midi2_msg_program(w, kCatalogGroup, kCatalogChannel,
                                              5, /*bank_valid*/ true, 0, 0);
            log_words(49, "program prog=5 bank_msb=0 bank_lsb=0", w, 2);
        } break;
        case 50: {
            midi.sendPerNotePitchBend(kCatalogGroup, kCatalogChannel, 60, 0x80000000u);
            uint32_t w[4]; midi2_msg_per_note_pb(w, kCatalogGroup, kCatalogChannel, 60, 0x80000000u);
            log_words(50, "per_note_pitch_bend n=60 val=0x80000000", w, 2);
        } break;
        case 51: {
            midi.sendPerNotePitchBend(kCatalogGroup, kCatalogChannel, 60, 0xC0000000u);
            uint32_t w[4]; midi2_msg_per_note_pb(w, kCatalogGroup, kCatalogChannel, 60, 0xC0000000u);
            log_words(51, "per_note_pitch_bend n=60 val=0xC0000000", w, 2);
        } break;
        case 52: {
            midi.sendPerNoteManagement(kCatalogGroup, kCatalogChannel, 60,
                                       /*detach*/ true, /*reset*/ true);
            uint32_t w[4]; midi2_msg_per_note_mgmt(w, kCatalogGroup, kCatalogChannel,
                                                    60, /*detach*/ true, /*reset*/ true);
            log_words(52, "per_note_mgmt n=60 detach=1 reset=1", w, 2);
        } break;
        case 53: {
            midi.sendRegPerNoteController(kCatalogGroup, kCatalogChannel, 60, 7, 0x80000000u);
            uint32_t w[4]; midi2_msg_reg_per_note_ctrl(w, kCatalogGroup, kCatalogChannel,
                                                        60, 7, 0x80000000u);
            log_words(53, "reg_per_note_ctrl n=60 idx=7 val=0x80000000", w, 2);
        } break;
        case 54: {
            midi.sendAsnPerNoteController(kCatalogGroup, kCatalogChannel, 60, 12, 0x80000000u);
            uint32_t w[4]; midi2_msg_asn_per_note_ctrl(w, kCatalogGroup, kCatalogChannel,
                                                        60, 12, 0x80000000u);
            log_words(54, "asn_per_note_ctrl n=60 idx=12 val=0x80000000", w, 2);
        } break;
        case 55: {
            midi.sendRpn(kCatalogGroup, kCatalogChannel, 0, 0, 0x10000000u);
            uint32_t w[4]; midi2_msg_rpn(w, kCatalogGroup, kCatalogChannel, 0, 0, 0x10000000u);
            log_words(55, "rpn bank=0 idx=0 val=0x10000000", w, 2);
        } break;
        case 56: {
            midi.sendNrpn(kCatalogGroup, kCatalogChannel, 1, 5, 0x20000000u);
            uint32_t w[4]; midi2_msg_nrpn(w, kCatalogGroup, kCatalogChannel, 1, 5, 0x20000000u);
            log_words(56, "nrpn bank=1 idx=5 val=0x20000000", w, 2);
        } break;
        case 57: {
            midi.sendRelRpn(kCatalogGroup, kCatalogChannel, 0, 0, 0x00000010);
            uint32_t w[4]; midi2_msg_rel_rpn(w, kCatalogGroup, kCatalogChannel,
                                              0, 0, 0x00000010u);
            log_words(57, "rel_rpn bank=0 idx=0 delta=+0x10", w, 2);
        } break;
        case 58: {
            midi.sendRelNrpn(kCatalogGroup, kCatalogChannel, 1, 5, (int32_t)0xFFFFFFF0);
            uint32_t w[4]; midi2_msg_rel_nrpn(w, kCatalogGroup, kCatalogChannel,
                                               1, 5, 0xFFFFFFF0u);
            log_words(58, "rel_nrpn bank=1 idx=5 delta=-0x10", w, 2);
        } break;

        /* ============================================================
         * §5.5 MIDI 1.0 Channel Voice (mt 0x2)
         * ============================================================ */
        case 59: {
            midi.sendNoteOn1(kCatalogGroup, kCatalogChannel, 60, 0x40);
            log_word1(59, "midi1 note_on n=60 vel=0x40",
                      ((uint32_t)0x2 << 28) | ((uint32_t)kCatalogGroup << 24)
                      | ((uint32_t)0x90 << 16) | ((uint32_t)60 << 8)  | 0x40);
        } break;
        case 60: {
            midi.sendNoteOff1(kCatalogGroup, kCatalogChannel, 60, 0x00);
            log_word1(60, "midi1 note_off n=60 vel=0x00",
                      ((uint32_t)0x2 << 28) | ((uint32_t)kCatalogGroup << 24)
                      | ((uint32_t)0x80 << 16) | ((uint32_t)60 << 8)  | 0x00);
        } break;
        case 61: {
            midi.sendCC1(kCatalogGroup, kCatalogChannel, 7, 0x60);
            log_word1(61, "midi1 cc cc=7 val=0x60",
                      ((uint32_t)0x2 << 28) | ((uint32_t)kCatalogGroup << 24)
                      | ((uint32_t)0xB0 << 16) | ((uint32_t)7 << 8)   | 0x60);
        } break;
        case 62: {
            const uint16_t v14 = 0x2000u;  // LSB=0x00 MSB=0x40
            midi.sendPitchBend1(kCatalogGroup, kCatalogChannel, v14);
            log_word1(62, "midi1 pitch_bend center 0x2000",
                      ((uint32_t)0x2 << 28) | ((uint32_t)kCatalogGroup << 24)
                      | ((uint32_t)0xE0 << 16) | ((uint32_t)(v14 & 0x7F) << 8)
                      | ((v14 >> 7) & 0x7F));
        } break;
        case 63: {
            midi.sendProgram1(kCatalogGroup, kCatalogChannel, 5);
            log_word1(63, "midi1 program prog=5",
                      ((uint32_t)0x2 << 28) | ((uint32_t)kCatalogGroup << 24)
                      | ((uint32_t)0xC0 << 16) | ((uint32_t)5 << 8));
        } break;

        /* ============================================================
         * §5.6 System Common / Real-Time (mt 0x1)
         * ============================================================ */
        case 64: {
            // Timing Clock x24 in sequence (1 quarter @ 24 ppqn).
            uint32_t w0 = midi2_msg_system_timing_clock(kCatalogGroup);
            for (uint8_t k = 0; k < 24; ++k) {
                midi.sendClock(kCatalogGroup);
                log_word1(64, "system timing_clock (1 of 24)", w0);
            }
        } break;
        case 65: {
            midi.sendStart(kCatalogGroup);
            log_word1(65, "system start", midi2_msg_system_start(kCatalogGroup));
        } break;
        case 66: {
            midi.sendContinue(kCatalogGroup);
            log_word1(66, "system continue", midi2_msg_system_continue(kCatalogGroup));
        } break;
        case 67: {
            midi.sendStop(kCatalogGroup);
            log_word1(67, "system stop", midi2_msg_system_stop(kCatalogGroup));
        } break;
        case 68: {
            const uint8_t time_code = 0x05;  // type=0, value=0x05
            midi.sendMTC(kCatalogGroup, time_code);
            log_word1(68, "system mtc qframe time_code=0x05",
                      midi2_msg_system_mtc(kCatalogGroup, time_code));
        } break;
        case 69: {
            const uint16_t pos14 = 0x0010u;
            midi.sendSongPosition(kCatalogGroup, pos14);
            log_word1(69, "system song_position 14b=0x0010",
                      midi2_msg_system_song_position(kCatalogGroup, pos14));
        } break;
        case 70: {
            midi.sendSongSelect(kCatalogGroup, 3);
            log_word1(70, "system song_select song=3",
                      midi2_msg_system_song_select(kCatalogGroup, 3));
        } break;
        case 71: {
            midi.sendTuneRequest(kCatalogGroup);
            log_word1(71, "system tune_request",
                      midi2_msg_system_tune_request(kCatalogGroup));
        } break;
        case 72: {
            midi.sendActiveSensing(kCatalogGroup);
            log_word1(72, "system active_sensing",
                      midi2_msg_system_active_sensing(kCatalogGroup));
        } break;
        case 73: {
            midi.sendSystemReset(kCatalogGroup);
            log_word1(73, "system reset",
                      midi2_msg_system_reset(kCatalogGroup));
        } break;

        /* ============================================================
         * §5.7 Data Messages (SysEx7 mt 0x3, SysEx8 mt 0x5)
         *
         * For SysEx7 we use pumpRaw + builder to control the packet
         * boundaries exactly (the midi.sendSysEx7 helper would emit a
         * Complete-form by default, but the spec asks for explicit
         * Start/Continue/End packets).
         * ============================================================ */
        case 74: {
            const uint8_t data[] = {'H', 'e', 'l', 'l', 'o', '!'};
            midi.sendSysEx7(kCatalogGroup, data, sizeof(data));
            uint32_t w[4] = {0,0,0,0};
            midi2_msg_sysex7_packet(w, kCatalogGroup, /*Complete*/ 0x00, data, sizeof(data));
            log_words(74, "sysex7 complete 'Hello!'", w, 2);
        } break;
        case 75: {
            const uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' '};
            emit_sysex7_one(75, midi, /*Start*/ 0x10, data, sizeof(data),
                            "sysex7 start 'Hello '");
        } break;
        case 76: {
            const uint8_t data[] = {'W', 'o', 'r', 'l', 'd', ' '};
            emit_sysex7_one(76, midi, /*Continue*/ 0x20, data, sizeof(data),
                            "sysex7 continue 'World '");
        } break;
        case 77: {
            const uint8_t data[] = {'D', 'o', 'n', 'e'};
            emit_sysex7_one(77, midi, /*End*/ 0x30, data, sizeof(data),
                            "sysex7 end 'Done'");
        } break;
        case 78: {
            const uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'U', 'M', 'P', ' ', '8', '!'};
            // 12 data bytes; midi.sendSysEx8 fragments if needed but 12 fits.
            midi.sendSysEx8(kCatalogGroup, /*streamId*/ 1, data, sizeof(data));
            // Mixed Data Set: one header + one payload chunk (MT 0x5).
            static const uint8_t mdsData[] = {0x7D, 0x4D, 0x44, 0x53};
            midi.sendMds(kCatalogGroup, /*mdsId*/ 1, mdsData, sizeof mdsData, /*mfrId*/ 0x7D00);
            uint32_t w[4];
            midi2_msg_sysex8_packet(w, kCatalogGroup, /*Complete*/ 0x00, /*streamId*/ 1,
                                     data, sizeof(data));
            log_words(78, "sysex8 complete 'Hello UMP 8!'", w, 4);
        } break;
        case 79: {
            const uint8_t data[13] = {'A','B','C','D','E','F','G','H','I','J','K','L','M'};
            emit_sysex8_one(79, midi, /*Start*/ 0x10, /*streamId*/ 1, data, sizeof(data),
                            "sysex8 start 13 bytes A..M");
        } break;
        case 80: {
            const uint8_t data[13] = {'N','O','P','Q','R','S','T','U','V','W','X','Y','Z'};
            emit_sysex8_one(80, midi, /*Continue*/ 0x20, /*streamId*/ 1, data, sizeof(data),
                            "sysex8 continue 13 bytes N..Z");
        } break;
        case 81: {
            const uint8_t data[13] = {'0','1','2','3','4','5','6','7','8','9','!','?','.'};
            emit_sysex8_one(81, midi, /*End*/ 0x30, /*streamId*/ 1, data, sizeof(data),
                            "sysex8 end 13 bytes digits");
        } break;

        /* ============================================================
         * §5.8 UMP Stream (mt 0xF)
         * ============================================================ */
        case 82: {
            // Endpoint Discovery is normally a HOST request; the bench
            // emits one for monitor-side coverage via pumpRaw.
            uint32_t w[4];
            midi2_msg_stream_endpoint_discovery(w, /*ump_ver_major*/ 1,
                                                 /*ump_ver_minor*/ 1,
                                                 /*filter*/ 0xFF);
            midi2_board::pumpRaw(w, 4);
            log_words(82, "stream endpoint_discovery filter=0xFF", w, 4);
        } break;
        case 83: {
            midi.sendEndpointInfo(/*ump_ver_major*/ 1, /*minor*/ 1,
                                  /*static_fb*/ true, /*num_fb*/ 1,
                                  /*midi2*/ true, /*midi1*/ true,
                                  /*rx_jr*/ false, /*tx_jr*/ true);
            uint32_t w[4];
            midi2_msg_stream_endpoint_info(w, 1, 1, true, 1, true, true, false, true);
            log_words(83, "stream endpoint_info v1.1 midi1+midi2 tx_jr", w, 4);
        } break;
        case 84: {
            midi.sendDeviceIdentity(kBenchMfrId, kBenchFamily, kBenchModel, kBenchVersion);
            uint32_t w[4];
            const uint32_t mfr24 = ((uint32_t)kBenchMfrId[0] << 16)
                                  | ((uint32_t)kBenchMfrId[1] << 8)
                                  |  (uint32_t)kBenchMfrId[2];
            midi2_msg_stream_device_identity(w, mfr24, kBenchFamily, kBenchModel, kBenchVersion);
            log_words(84, "stream device_identity bench", w, 4);
        } break;
        case 85: {
            midi.sendEndpointNameUpdate(kBenchEndpoint);
            emit_endpoint_name_text(85, kBenchEndpoint, "stream endpoint_name");
        } break;
        case 86: {
            midi.sendProductInstanceIdUpdate(kBenchProductInst);
            emit_product_id_text(86, kBenchProductInst, "stream product_instance_id");
        } break;
        case 87: {
            // Stream Configuration Request, no midi2cpp sender (host-side message).
            uint32_t w[4]; midi2_msg_stream_config_request(w, /*protocol*/ 0x02, false, false);
            midi2_board::pumpRaw(w, 4);
            log_words(87, "stream config_request protocol=2", w, 4);
        } break;
        case 88: {
            midi.sendStreamConfigNotify(/*protocol*/ 0x02);
            uint32_t w[4]; midi2_msg_stream_config_notify(w, /*protocol*/ 0x02, false, true);
            log_words(88, "stream config_notify protocol=2", w, 4);
        } break;
        case 89: {
            // FB Discovery, no midi2cpp sender (host-side message).
            uint32_t w[4]; midi2_msg_stream_fb_discovery(w, /*fb_num*/ 0xFF, /*filter*/ 0xFF);
            midi2_board::pumpRaw(w, 4);
            log_words(89, "stream fb_discovery fb=0xFF filter=0xFF", w, 4);
        } break;
        case 90: {
            midi.sendFbInfo(/*active*/ true, /*fb_num*/ 0,
                            /*direction*/ 0x03, /*ui_hint*/ 0x02,
                            /*first_group*/ 0, /*num_groups*/ 1,
                            /*midi_ci_ver*/ 0x02, /*sysex8*/ false,
                            /*protocol*/ 0x02);
            uint32_t w[4];
            midi2_msg_stream_fb_info(w, true, 0, /*direction*/ 0x03,
                                     /*ui_hint*/ 0x02, 0, 1, 0x02,
                                     /*max_sysex8*/ 0, 0x02);
            log_words(90, "stream fb_info fb=0 bidir grp0..0", w, 4);
        } break;
        case 91: {
            midi.sendFbNameUpdate(0, kBenchFbName);
            emit_fb_name_text(91, /*fb_num*/ 0, kBenchFbName, "stream fb_name");
        } break;
        case 92: {
            midi.sendStartOfClip();
            uint32_t w[4]; midi2_msg_stream_start_of_clip(w);
            log_words(92, "stream start_of_clip", w, 4);
        } break;
        case 93: {
            midi.sendEndOfClip();
            uint32_t w[4]; midi2_msg_stream_end_of_clip(w);
            log_words(93, "stream end_of_clip", w, 4);
        } break;

        /* ============================================================
         * §5.9 Utility (mt 0x0)
         * ============================================================ */
        case 94: {
            midi.sendNoop(kCatalogGroup);
            log_word1(94, "utility noop",
                      ((uint32_t)0x0 << 28) | ((uint32_t)kCatalogGroup << 24));
        } break;
        case 95: {
            midi.sendJRClock(kCatalogGroup, 0x4000);
            log_word1(95, "utility jr_clock ts=0x4000",
                      midi2_msg_jr_clock(0x4000));
        } break;
        case 96: {
            midi.sendJRTimestamp(kCatalogGroup, 0x2000);
            log_word1(96, "utility jr_timestamp ts=0x2000",
                      midi2_msg_jr_timestamp(0x2000));
        } break;
        case 97: {
            midi.sendDctpq(480);
            log_word1(97, "utility dctpq tpq=480", midi2_msg_dctpq(480));
        } break;
        case 98: {
            midi.sendDeltaClockstamp(240);
            log_word1(98, "utility delta_clockstamp ticks=240",
                      midi2_msg_delta_clockstamp(240));
        } break;

        /* ============================================================
         * §5.10 Edge cases
         * ============================================================ */
        case 99: {
            // Set Tempo BPM=120 with bit 31 of word2 set (reserved bits).
            uint32_t w[4];
            midi2_msg_tempo(w, kCatalogGroup, /*ten_ns*/ 50000000u);
            w[2] |= (UINT32_C(1) << 31);
            midi2_board::pumpRaw(w, 4);
            log_words(99, "edge: set_tempo with reserved bit set", w, 4);
        } break;
        case 100: {
            // Flex Data bank 0x00 with status 0x42 (unassigned in M2-104 v1.1.2).
            uint32_t w[4] = {0, 0, 0, 0};
            w[0] = ((uint32_t)0xD << 28)             // mt 0xD
                 | ((uint32_t)kCatalogGroup << 24)
                 | ((uint32_t)0x00 << 22)            // form 0
                 | ((uint32_t)0x01 << 20)            // address group-level
                 | ((uint32_t)0x00 << 16)            // channel 0
                 | ((uint32_t)0x00 << 8)             // statusBank 0x00
                 |  (uint32_t)0x42;                  // status 0x42 (unassigned)
            midi2_board::pumpRaw(w, 4);
            log_words(100, "edge: flex_data bank=0x00 status=0x42", w, 4);
        } break;

        default:
            emit_stub(idx);
            break;
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

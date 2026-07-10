/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Saulo Verissimo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Auto-generated from midi2 v0.7.0 (reproducible: no timestamp)
 * https://github.com/sauloverissimo/midi2
 *
 * Portable MIDI 2.0 library (C99, zero dependencies)
 * Specs: MIDI 2.0 UMP (M2-104-UM v1.1.2), MIDI-CI (M2-101-UM v1.2)
 *
 * Usage:
 *   #include "midi2.h"
 *   // In exactly ONE .c file:
 *   #define MIDI2_IMPLEMENTATION
 *   #include "midi2.h"
 */

#ifndef MIDI2_H
#define MIDI2_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* == midi2_msg =========================================================== */


/*
 * midi2_msg.h - UMP message construction and parsing
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI 2.0 UMP (M2-104-UM v1.1.2, Nov 2024)
 */



/* Overridable assertion for debug-build contract checks (programmer errors).
 * Compiles out under NDEBUG. Define MIDI2_ASSERT before including midi2 to
 * override it (e.g. a custom fault handler, or a no-op). Zero-dependency:
 * defaults to the standard assert. */
#ifndef MIDI2_ASSERT
#  include <assert.h>
#  define MIDI2_ASSERT(x) assert(x)
#endif


/*--------------------------------------------------------------------+
 * Internal helper: conditional bit set
 *
 * MIDI2_BIT_IF(cond, n) → (1u << n) if cond is true, else 0u.
 * Used to pack multiple boolean flags into bitfield words concisely.
 * Compile-time const expression when cond and n are constants.
 *--------------------------------------------------------------------*/
#define MIDI2_BIT_IF(cond, n) ((cond) ? (UINT32_C(1) << (n)) : UINT32_C(0))

/*--------------------------------------------------------------------+
 * UMP Message Types (bits [31:28] of word 0)
 *--------------------------------------------------------------------*/
enum {
  MIDI2_MT_UTILITY    = 0x00,  /* 1 word  */
  MIDI2_MT_SYSTEM     = 0x01,  /* 1 word  */
  MIDI2_MT_MIDI1_CV   = 0x02,  /* 1 word  */
  MIDI2_MT_SYSEX7     = 0x03,  /* 2 words */
  MIDI2_MT_MIDI2_CV   = 0x04,  /* 2 words */
  MIDI2_MT_DATA128    = 0x05,  /* 4 words */
  MIDI2_MT_FLEX_DATA  = 0x0D,  /* 4 words */
  MIDI2_MT_STREAM     = 0x0F,  /* 4 words */
};

/*--------------------------------------------------------------------+
 * MIDI 2.0 Channel Voice Status (upper nibble, MT 0x4)
 *--------------------------------------------------------------------*/
enum {
  MIDI2_STATUS_NOTE_OFF      = 0x80,
  MIDI2_STATUS_NOTE_ON       = 0x90,
  MIDI2_STATUS_POLY_PRESSURE = 0xA0,
  MIDI2_STATUS_CC            = 0xB0,
  MIDI2_STATUS_PROGRAM       = 0xC0,
  MIDI2_STATUS_CHAN_PRESSURE  = 0xD0,
  MIDI2_STATUS_PITCH_BEND    = 0xE0,
  MIDI2_STATUS_PER_NOTE_MGMT = 0xF0,
  MIDI2_STATUS_REG_PER_NOTE  = 0x00,  /* Registered Per-Note Controller */
  MIDI2_STATUS_ASN_PER_NOTE  = 0x10,  /* Assignable Per-Note Controller */
  MIDI2_STATUS_RPN           = 0x20,  /* Registered Controller (RPN) */
  MIDI2_STATUS_NRPN          = 0x30,  /* Assignable Controller (NRPN) */
  MIDI2_STATUS_REL_RPN       = 0x40,  /* Relative Registered Controller */
  MIDI2_STATUS_REL_NRPN      = 0x50,  /* Relative Assignable Controller */
  MIDI2_STATUS_PER_NOTE_PB   = 0x60,
  /* Note: Per-Note CC uses the same opcode as ASN_PER_NOTE (0x10).
   * Use MIDI2_STATUS_ASN_PER_NOTE for new code. */
};

/*--------------------------------------------------------------------+
 * SysEx7 Status
 *--------------------------------------------------------------------*/
enum {
  MIDI2_SYSEX7_COMPLETE = 0x00,
  MIDI2_SYSEX7_START    = 0x10,
  MIDI2_SYSEX7_CONTINUE = 0x20,
  MIDI2_SYSEX7_END      = 0x30,
};

/*--------------------------------------------------------------------+
 * Flex Data Status
 *--------------------------------------------------------------------*/
enum {
  MIDI2_FLEX_TEMPO      = 0x00,
  MIDI2_FLEX_TIME_SIG   = 0x01,
  MIDI2_FLEX_METRONOME  = 0x02,
  MIDI2_FLEX_KEY_SIG    = 0x05,
  MIDI2_FLEX_CHORD_NAME = 0x06,
};

/* Flex Data Status Banks */
enum {
  MIDI2_FLEX_BANK_SETUP    = 0x00,  /* Setup & Performance Events */
  MIDI2_FLEX_BANK_METADATA = 0x01,  /* Metadata Text */
  MIDI2_FLEX_BANK_PERF_TEXT = 0x02, /* Performance Text Events (lyrics) */
};

/* Flex Data Metadata Text status values (bank 0x01) */
enum {
  MIDI2_FLEX_TEXT_UNKNOWN          = 0x00,
  MIDI2_FLEX_TEXT_PROJECT_NAME     = 0x01,
  MIDI2_FLEX_TEXT_COMPOSITION_NAME = 0x02,
  MIDI2_FLEX_TEXT_CLIP_NAME        = 0x03,
  MIDI2_FLEX_TEXT_COPYRIGHT        = 0x04,
  MIDI2_FLEX_TEXT_COMPOSER_NAME    = 0x05,
  MIDI2_FLEX_TEXT_LYRICIST_NAME    = 0x06,
  MIDI2_FLEX_TEXT_ARRANGER_NAME    = 0x07,
  MIDI2_FLEX_TEXT_PUBLISHER_NAME   = 0x08,
  MIDI2_FLEX_TEXT_PERFORMER_NAME   = 0x09,
  MIDI2_FLEX_TEXT_ACCOMPANY_NAME   = 0x0A,
  MIDI2_FLEX_TEXT_RECORDING_DATE   = 0x0B,
  MIDI2_FLEX_TEXT_RECORDING_LOC    = 0x0C,
};

/* Flex Data Performance Text status values (bank 0x02) */
enum {
  MIDI2_FLEX_PERF_UNKNOWN        = 0x00,
  MIDI2_FLEX_PERF_LYRICS         = 0x01,
  MIDI2_FLEX_PERF_LYRICS_LANG    = 0x02,
  MIDI2_FLEX_PERF_RUBY           = 0x03,
  MIDI2_FLEX_PERF_RUBY_LANG      = 0x04,
};

/*--------------------------------------------------------------------+
 * Word Count
 *
 * Word count per Message Type, covering all 16 MTs per UMP 1.1.2
 * sec 2.1.4 (MT Allocation). Reserved MTs (0x6, 0x7, 0x8, 0x9, 0xA,
 * 0xB, 0xC, 0xE) map to their spec-defined length so an unknown stream
 * advances by the right amount instead of cascading misalignment.
 *
 * Accepts any uint8_t; only the low nibble is consulted (defensive
 * mask, mt > 0x0F is treated as mt & 0x0F).
 *--------------------------------------------------------------------*/
static inline uint8_t midi2_msg_word_count(uint8_t mt) {
  /* Positional initializer (index 0x0..0xF). C99 and C++ both accept it;
   * designated array initializers do not compile under C++. */
  static const uint8_t lut[16] = {
    1, 1, 1, 2,  /* 0x0 utility, 0x1 system, 0x2 MIDI1 CV, 0x3 SysEx7 */
    2, 4, 1, 1,  /* 0x4 MIDI2 CV, 0x5 Data128, 0x6/0x7 reserved 32-bit */
    2, 2, 2, 3,  /* 0x8/0x9/0xA reserved 64-bit, 0xB reserved 96-bit */
    3, 4, 4, 4,  /* 0xC reserved 96-bit, 0xD Flex Data, 0xE reserved 128, 0xF UMP Stream */
  };
  return lut[mt & 0x0F];
}

/*--------------------------------------------------------------------+
 * Parsing (field extraction from word 0)
 *--------------------------------------------------------------------*/

/** @brief Extract message type (bits [31:28] of word 0). */
static inline uint8_t  midi2_msg_get_mt(const uint32_t *w)       { return (uint8_t)((w[0] >> 28) & 0x0F); }
/** @brief Extract group number (bits [27:24] of word 0). Range: 0-15. */
static inline uint8_t  midi2_msg_get_group(const uint32_t *w)    { return (uint8_t)((w[0] >> 24) & 0x0F); }
/** @brief Extract full status byte (bits [23:16] of word 0). Includes status nibble + channel. */
static inline uint8_t  midi2_msg_get_status(const uint32_t *w)   { return (uint8_t)((w[0] >> 16) & 0xFF); }
/** @brief Extract channel number (bits [19:16] of word 0). Range: 0-15. */
static inline uint8_t  midi2_msg_get_channel(const uint32_t *w)  { return (uint8_t)((w[0] >> 16) & 0x0F); }
/** @brief Extract note/index field (bits [15:8] of word 0). Full 8-bit field. */
static inline uint8_t  midi2_msg_get_note(const uint32_t *w)     { return (uint8_t)((w[0] >> 8) & 0xFF); }
/** @brief Extract velocity (bits [31:16] of word 1). 16-bit MIDI 2.0 velocity. */
static inline uint16_t midi2_msg_get_velocity(const uint32_t *w) { return (uint16_t)(w[1] >> 16); }
/** @brief Extract full word 1 (32-bit data payload). */
static inline uint32_t midi2_msg_get_data(const uint32_t *w)     { return w[1]; }

/** @brief Rewrite the Group field of a UMP word in-place.
 *
 *  Only MT 0x2 (MIDI 1.0 CV), 0x3 (SysEx7), 0x4 (MIDI 2.0 CV) and
 *  0x5 (Data128/SysEx8) carry a Group field in word 0 bits [27:24].
 *  Utility, System Real-Time, Flex Data and UMP Stream words have
 *  no Group field and are left untouched.
 *
 *  Useful for routing pipelines that need to re-stamp the group of
 *  forwarded messages without rebuilding the word from scratch.
 *  (v0.3.0+) */
static inline void midi2_msg_set_group(uint32_t *word0, uint8_t group) {
  uint8_t mt = (uint8_t)((*word0 >> 28) & 0x0Fu);
  if (mt >= 0x2u && mt <= 0x5u) {
    *word0 = (*word0 & 0xF0FFFFFFu) | ((uint32_t)(group & 0x0Fu) << 24);
  }
}

/*--------------------------------------------------------------------+
 * Value Scaling (MIDI 2.0 spec section 4.2.1)
 *
 * Bit-replication for symmetric round-trip: scaleDown(scaleUp(x)) == x
 *--------------------------------------------------------------------*/

/** @brief Scale 7-bit (0-127) to 16-bit (0-65535) with bit-replication. */
static inline uint16_t midi2_msg_scale_up_7to16(uint8_t v) {
  uint16_t x = (uint16_t)(v & 0x7F);
  return (uint16_t)((x << 9) | (x << 2) | (x >> 5));
}

/** @brief Scale 7-bit (0-127) to 32-bit (0-0xFFFFFFFF) with bit-replication. */
static inline uint32_t midi2_msg_scale_up_7to32(uint8_t v) {
  uint32_t x = (uint32_t)(v & 0x7F);
  return (x << 25) | (x << 18) | (x << 11) | (x << 4) | (x >> 3);
}

/** @brief Scale 14-bit (0-16383) to 32-bit (0-0xFFFFFFFF) with bit-replication. */
static inline uint32_t midi2_msg_scale_up_14to32(uint16_t v) {
  uint32_t x = (uint32_t)(v & 0x3FFF);
  return (x << 18) | (x << 4) | (x >> 10);
}

/** @brief Scale 16-bit to 7-bit. */
static inline uint8_t  midi2_msg_scale_down_16to7(uint16_t v)  { return (uint8_t)(v >> 9); }
/** @brief Scale 32-bit to 7-bit. */
static inline uint8_t  midi2_msg_scale_down_32to7(uint32_t v)  { return (uint8_t)(v >> 25); }
/** @brief Scale 32-bit to 14-bit. */
static inline uint16_t midi2_msg_scale_down_32to14(uint32_t v) { return (uint16_t)(v >> 18); }

/*--------------------------------------------------------------------+
 * MIDI 2.0 Channel Voice Construction (MT 0x4, 2 words)
 *
 * All write into a uint32_t w[2] provided by caller.
 *--------------------------------------------------------------------*/

/* Internal: build word 0 for MT 0x4 */
static inline uint32_t midi2_msg_build_cv2_w0(uint8_t group, uint8_t status,
                                          uint8_t channel, uint8_t b3, uint8_t b4) {
  return ((uint32_t)MIDI2_MT_MIDI2_CV << 28)
       | ((uint32_t)(group & 0x0F) << 24)
       | ((uint32_t)(status & 0xF0) << 16)
       | ((uint32_t)(channel & 0x0F) << 16)
       | ((uint32_t)b3 << 8)
       | (uint32_t)b4;
}

/**
 * @brief Build a MIDI 2.0 Note On message (MT 0x4, 2 words).
 * @param w         Output: uint32_t[2] provided by caller.
 * @param group     UMP group (0-15).
 * @param channel   MIDI channel (0-15).
 * @param note      Note number (0-127).
 * @param velocity  16-bit velocity (0x0000-0xFFFF). Use midi2_msg_scale_up_7to16() for legacy values.
 * @param attr_type Attribute type byte (0=none, 0x01=Manufacturer, 0x02=Profile, 0x03=Pitch7_9).
 * @param attr_data Attribute data (16 bits). Layout depends on attr_type. */
static inline void midi2_msg_note_on(uint32_t *w, uint8_t group, uint8_t channel,
                                      uint8_t note, uint16_t velocity,
                                      uint8_t attr_type, uint16_t attr_data) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_NOTE_ON, channel,
                                 note & 0x7F, attr_type);
  w[1] = ((uint32_t)velocity << 16) | (uint32_t)attr_data;
}

/** @brief Build a MIDI 2.0 Note Off message (MT 0x4, 2 words). See midi2_msg_note_on() for params. */
static inline void midi2_msg_note_off(uint32_t *w, uint8_t group, uint8_t channel,
                                       uint8_t note, uint16_t velocity,
                                       uint8_t attr_type, uint16_t attr_data) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_NOTE_OFF, channel,
                                 note & 0x7F, attr_type);
  w[1] = ((uint32_t)velocity << 16) | (uint32_t)attr_data;
}

/** @brief Build a MIDI 2.0 Control Change (MT 0x4). @param value 32-bit CC value. */
static inline void midi2_msg_cc(uint32_t *w, uint8_t group, uint8_t channel,
                                 uint8_t index, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_CC, channel, index & 0x7F, 0);
  w[1] = value;
}

/** @brief Build a MIDI 2.0 Program Change (MT 0x4, 2 words) per M2-104-UM section 7.4.9.
 *  @param bank_valid Bank Valid (B) bit goes in the byte 4 option flags (LSB).
 *
 *  Wire layout per Table 30:
 *    byte 3 = reserved (0)
 *    byte 4 = option flags: bit 0 = Bank Valid (B), other bits reserved (0)
 *    byte 5 = rppppppp (r reserved, p = program 0..127)
 *    byte 6 = reserved
 *    byte 7 = rBBBBBBB (bank MSB 0..127)
 *    byte 8 = rbbbbbbb (bank LSB 0..127)
 *
 *  Spec note: if bank_valid is false the sender shall fill bank MSB and bank
 *  LSB with zero. */
static inline void midi2_msg_program(uint32_t *w, uint8_t group, uint8_t channel,
                                      uint8_t program, bool bank_valid,
                                      uint8_t bank_msb, uint8_t bank_lsb) {
  uint8_t option_flags = bank_valid ? 0x01 : 0x00;
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_PROGRAM, channel,
                                 /*byte 3*/ 0, /*byte 4 option flags*/ option_flags);
  w[1] = ((uint32_t)(program & 0x7F) << 24)
       | (bank_valid ? (((uint32_t)(bank_msb & 0x7F) << 8) | (bank_lsb & 0x7F)) : 0);
}

/** @brief Build a MIDI 2.0 Pitch Bend (MT 0x4). @param value 32-bit, 0x80000000 = center. */
static inline void midi2_msg_pitch_bend(uint32_t *w, uint8_t group, uint8_t channel,
                                         uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_PITCH_BEND, channel, 0, 0);
  w[1] = value;
}

/** @brief Build a MIDI 2.0 Channel Pressure (MT 0x4). @param pressure 32-bit pressure value. */
static inline void midi2_msg_chan_pressure(uint32_t *w, uint8_t group, uint8_t channel,
                                            uint32_t pressure) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_CHAN_PRESSURE, channel, 0, 0);
  w[1] = pressure;
}

/** @brief Build a MIDI 2.0 Polyphonic Key Pressure (MT 0x4). */
static inline void midi2_msg_poly_pressure(uint32_t *w, uint8_t group, uint8_t channel,
                                             uint8_t note, uint32_t pressure) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_POLY_PRESSURE, channel, note & 0x7F, 0);
  w[1] = pressure;
}

/** @brief Build a MIDI 2.0 RPN message (MT 0x4). @param msb RPN bank. @param lsb RPN index. */
static inline void midi2_msg_rpn(uint32_t *w, uint8_t group, uint8_t channel,
                                   uint8_t msb, uint8_t lsb, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_RPN, channel, msb & 0x7F, lsb & 0x7F);
  w[1] = value;
}

/** @brief Build a MIDI 2.0 NRPN message (MT 0x4). Non-registered parameter. */
static inline void midi2_msg_nrpn(uint32_t *w, uint8_t group, uint8_t channel,
                                    uint8_t msb, uint8_t lsb, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_NRPN, channel, msb & 0x7F, lsb & 0x7F);
  w[1] = value;
}

/*--------------------------------------------------------------------+
 * Per-Note Expression (MT 0x4, 2 words)
 *--------------------------------------------------------------------*/
/** @brief Build a per-note Pitch Bend (MT 0x4). Independent pitch per note. */
static inline void midi2_msg_per_note_pb(uint32_t *w, uint8_t group, uint8_t channel,
                                           uint8_t note, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_PER_NOTE_PB, channel, note & 0x7F, 0);
  w[1] = value;
}

/** @brief Build a per-note CC (MT 0x4). Independent controller per note. */
static inline void midi2_msg_per_note_cc(uint32_t *w, uint8_t group, uint8_t channel,
                                           uint8_t note, uint8_t index, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_ASN_PER_NOTE, channel, note & 0x7F, index);
  w[1] = value;
}

/** @brief Build a per-note Management message (MT 0x4). @param detach release from voice. @param reset reset controllers. */
static inline void midi2_msg_per_note_mgmt(uint32_t *w, uint8_t group, uint8_t channel,
                                             uint8_t note, bool detach, bool reset) {
  uint8_t flags = (detach ? 0x02 : 0) | (reset ? 0x01 : 0);
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_PER_NOTE_MGMT, channel, note & 0x7F, flags);
  w[1] = 0;
}

/*--------------------------------------------------------------------+
 * Registered Per-Note Controller (MT 0x4, status 0x0)
 * 256 controllers per note, defined by MMA/AMEI.
 *--------------------------------------------------------------------*/
/** @brief Build a Registered Per-Note Controller message (MT 0x4).
 *  @param index Controller index (0-255). */
static inline void midi2_msg_reg_per_note_ctrl(uint32_t *w, uint8_t group, uint8_t channel,
                                                  uint8_t note, uint8_t index, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_REG_PER_NOTE, channel, note & 0x7F, index);
  w[1] = value;
}

/** @brief Build an Assignable Per-Note Controller message (MT 0x4).
 *  @param index Controller index (0-255). Application-specific. */
static inline void midi2_msg_asn_per_note_ctrl(uint32_t *w, uint8_t group, uint8_t channel,
                                                  uint8_t note, uint8_t index, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_ASN_PER_NOTE, channel, note & 0x7F, index);
  w[1] = value;
}

/*--------------------------------------------------------------------+
 * Relative RPN/NRPN (MT 0x4, status 0x4/0x5)
 * Two's complement relative value (positive = increase, negative = decrease).
 * Cannot be translated to MIDI 1.0.
 *--------------------------------------------------------------------*/
/** @brief Build a Relative Registered Controller (RPN) message. */
static inline void midi2_msg_rel_rpn(uint32_t *w, uint8_t group, uint8_t channel,
                                       uint8_t msb, uint8_t lsb, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_REL_RPN, channel, msb & 0x7F, lsb & 0x7F);
  w[1] = value;
}

/** @brief Build a Relative Assignable Controller (NRPN) message. */
static inline void midi2_msg_rel_nrpn(uint32_t *w, uint8_t group, uint8_t channel,
                                        uint8_t msb, uint8_t lsb, uint32_t value) {
  w[0] = midi2_msg_build_cv2_w0(group, MIDI2_STATUS_REL_NRPN, channel, msb & 0x7F, lsb & 0x7F);
  w[1] = value;
}

/*--------------------------------------------------------------------+
 * System Messages (MT 0x1, 1 word)
 *--------------------------------------------------------------------*/
static inline uint32_t midi2_msg_system(uint8_t group, uint8_t status) {
  return ((uint32_t)MIDI2_MT_SYSTEM << 28)
       | ((uint32_t)(group & 0x0F) << 24)
       | ((uint32_t)status << 16);
}

static inline uint32_t midi2_msg_system_2byte(uint8_t group, uint8_t status, uint8_t data1) {
  return midi2_msg_system(group, status) | ((uint32_t)data1 << 8);
}

static inline uint32_t midi2_msg_system_3byte(uint8_t group, uint8_t status,
                                                uint8_t data1, uint8_t data2) {
  return midi2_msg_system(group, status) | ((uint32_t)data1 << 8) | (uint32_t)data2;
}

/*--------------------------------------------------------------------+
 * System Real-Time + System Common named wrappers (M2-104-UM section 4.3,
 * v0.3.0+). Each calls the corresponding generic builder above with the
 * canonical status byte. Useful for pattern-matching senders and for
 * call sites that prefer the named shortcut over the magic-number form.
 * All inline; zero ROM cost when not called.
 *--------------------------------------------------------------------*/

/** @brief Tune Request (status 0xF6, 1-byte System Common). */
static inline uint32_t midi2_msg_system_tune_request(uint8_t group) {
  return midi2_msg_system(group, 0xF6);
}

/** @brief Timing Clock (status 0xF8, 1-byte System Real-Time). */
static inline uint32_t midi2_msg_system_timing_clock(uint8_t group) {
  return midi2_msg_system(group, 0xF8);
}

/** @brief Start (status 0xFA, 1-byte System Real-Time, sequencer start). */
static inline uint32_t midi2_msg_system_start(uint8_t group) {
  return midi2_msg_system(group, 0xFA);
}

/** @brief Continue (status 0xFB, 1-byte System Real-Time). */
static inline uint32_t midi2_msg_system_continue(uint8_t group) {
  return midi2_msg_system(group, 0xFB);
}

/** @brief Stop (status 0xFC, 1-byte System Real-Time). */
static inline uint32_t midi2_msg_system_stop(uint8_t group) {
  return midi2_msg_system(group, 0xFC);
}

/** @brief Active Sensing (status 0xFE, 1-byte System Real-Time). */
static inline uint32_t midi2_msg_system_active_sensing(uint8_t group) {
  return midi2_msg_system(group, 0xFE);
}

/** @brief System Reset (status 0xFF, 1-byte System Real-Time). */
static inline uint32_t midi2_msg_system_reset(uint8_t group) {
  return midi2_msg_system(group, 0xFF);
}

/** @brief MIDI Time Code Quarter Frame (status 0xF1, 2-byte System Common). */
static inline uint32_t midi2_msg_system_mtc(uint8_t group, uint8_t time_code) {
  return midi2_msg_system_2byte(group, 0xF1, time_code & 0x7F);
}

/** @brief Song Select (status 0xF3, 2-byte System Common). */
static inline uint32_t midi2_msg_system_song_select(uint8_t group, uint8_t song) {
  return midi2_msg_system_2byte(group, 0xF3, song & 0x7F);
}

/** @brief Song Position Pointer (status 0xF2, 3-byte System Common).
 *  @param position 14-bit position; LSB stored at data1, MSB at data2. */
static inline uint32_t midi2_msg_system_song_position(uint8_t group, uint16_t position) {
  return midi2_msg_system_3byte(group, 0xF2,
                                 (uint8_t)(position & 0x7F),
                                 (uint8_t)((position >> 7) & 0x7F));
}

/*--------------------------------------------------------------------+
 * Flex Data (MT 0xD, 4 words)
 *
 * Word 0: [MT:4][group:4][format:2][address:2][channel:4][statusBank:8][status:8]
 * Tempo, TimeSignature, KeySignature are group-level (address=0b01).
 *--------------------------------------------------------------------*/

/* Internal: flex data word 0 builder with bank, address, channel, and format */
static inline uint32_t midi2_msg_build_flex_w0_full(uint8_t group, uint8_t format,
                                                       uint8_t address, uint8_t channel,
                                                       uint8_t bank, uint8_t status) {
  return ((uint32_t)MIDI2_MT_FLEX_DATA << 28)
       | ((uint32_t)(group & 0x0F) << 24)
       | ((uint32_t)(format & 0x03) << 22)
       | ((uint32_t)(address & 0x03) << 20)
       | ((uint32_t)(channel & 0x0F) << 16)
       | ((uint32_t)(bank & 0xFF) << 8)
       | (uint32_t)(status & 0xFF);
}

/* Internal: flex data word 0 builder (group-level shorthand) */
static inline uint32_t midi2_msg_build_flex_w0(uint8_t group, uint8_t status) {
  return midi2_msg_build_flex_w0_full(group, 0, 0x01, 0,
                                         MIDI2_FLEX_BANK_SETUP, status);
}

/* Tempo: word1 = 10ns per quarter note */
static inline void midi2_msg_tempo(uint32_t *w, uint8_t group, uint32_t ten_ns_per_qn) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_flex_w0(group, MIDI2_FLEX_TEMPO);
  w[1] = ten_ns_per_qn;
}

/** @brief Build a Set Time Signature message (MT 0xD, status 0x01)
 *         per M2-104-UM section 7.5.4.
 *  @param numerator       1..255 (256 is encoded as wrap).
 *  @param denominator     Negative power of 2 (2 = quarter note, 3 = eighth, ...).
 *                         0 indicates a non-standard denominator.
 *  @param num_32nd_notes  Number of 1/32 notes in 24 MIDI Clocks (SMF compat).
 *                         Default per SMF 1: 8 (= 8 thirty-seconds per quarter).
 *
 *  Wire layout word 1: [numerator:8][denominator:8][num_32nd_notes:8][reserved:8] */
static inline void midi2_msg_time_sig(uint32_t *w, uint8_t group,
                                        uint8_t numerator, uint8_t denominator,
                                        uint8_t num_32nd_notes) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_flex_w0(group, MIDI2_FLEX_TIME_SIG);
  w[1] = ((uint32_t)numerator << 24)
       | ((uint32_t)denominator << 16)
       | ((uint32_t)num_32nd_notes << 8);
}

/* Key Signature: word1 = [sharpsFlats:4][tonicNote:4][keyType:2][0:22]
 * sharpsFlats: -7 to +7 (4-bit signed). keyType: 0=major, 1=minor.
 * address: 0x0a = channel, 0x01 = group. tonic: 0=unknown, 1=A..7=G. */
static inline void midi2_msg_key_sig(uint32_t *w, uint8_t group,
                                       int8_t sharps_flats, bool minor) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_flex_w0(group, MIDI2_FLEX_KEY_SIG);
  uint8_t sf4 = (uint8_t)(sharps_flats & 0x0F);
  uint8_t key_type = minor ? 1 : 0;
  w[1] = ((uint32_t)sf4 << 28) | ((uint32_t)key_type << 22);
}

/** @brief Build a Key Signature with tonic note and channel addressing.
 *  @param address 0=channel, 1=group.
 *  @param tonic 0=unknown, 1=A, 2=B, 3=C, 4=D, 5=E, 6=F, 7=G.
 *  @param key_type 0=major, 1=minor, 2=none/atonal, 3=reserved. */
static inline void midi2_msg_key_sig_full(uint32_t *w, uint8_t group, uint8_t address,
                                             uint8_t channel, int8_t sharps_flats,
                                             uint8_t tonic, uint8_t key_type) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_flex_w0_full(group, 0, address, channel,
                                         MIDI2_FLEX_BANK_SETUP, MIDI2_FLEX_KEY_SIG);
  w[1] = ((uint32_t)(sharps_flats & 0x0F) << 28)
       | ((uint32_t)(tonic & 0x0F) << 24)
       | ((uint32_t)(key_type & 0x03) << 22);
}

/* Set Metronome: group-level (address=1), format=0 (complete).
 * primary_clicks: MIDI clocks per primary click
 * accent_1/2/3: bar accent parts (sum = beats in bar)
 * subdiv_1/2: subdivision clicks per primary click period */
static inline void midi2_msg_metronome(uint32_t *w, uint8_t group,
                                          uint8_t primary_clicks,
                                          uint8_t accent_1, uint8_t accent_2, uint8_t accent_3,
                                          uint8_t subdiv_1, uint8_t subdiv_2) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_flex_w0_full(group, 0, 0x01, 0,
                                         MIDI2_FLEX_BANK_SETUP, MIDI2_FLEX_METRONOME);
  w[1] = ((uint32_t)primary_clicks << 24)
       | ((uint32_t)accent_1 << 16)
       | ((uint32_t)accent_2 << 8)
       | (uint32_t)accent_3;
  w[2] = ((uint32_t)subdiv_1 << 24)
       | ((uint32_t)subdiv_2 << 16);
}

/* Set Chord Name: channel-addressable, format=0.
 * Tonic: sharps_flats (4-bit signed), tonic_note (0=unknown..7=G).
 * chord_type: 0x00=clear, 0x01=major, 0x07=minor, etc. (see spec Table 14)
 * Alterations: up to 4 for chord, up to 2 for bass. type: 0=none, 1=add, 2=sub, 3=raise, 4=lower.
 * Bass: bass_sharps_flats, bass_note, bass_chord_type. */
static inline void midi2_msg_chord_name(uint32_t *w, uint8_t group, uint8_t address,
                                           uint8_t channel,
                                           int8_t tonic_sf, uint8_t tonic_note, uint8_t chord_type,
                                           uint8_t alt1_type, uint8_t alt1_deg,
                                           uint8_t alt2_type, uint8_t alt2_deg,
                                           uint8_t alt3_type, uint8_t alt3_deg,
                                           uint8_t alt4_type, uint8_t alt4_deg,
                                           int8_t bass_sf, uint8_t bass_note, uint8_t bass_type,
                                           uint8_t bass_alt1_type, uint8_t bass_alt1_deg,
                                           uint8_t bass_alt2_type, uint8_t bass_alt2_deg) {
  w[0] = midi2_msg_build_flex_w0_full(group, 0, address, channel,
                                         MIDI2_FLEX_BANK_SETUP, MIDI2_FLEX_CHORD_NAME);
  w[1] = ((uint32_t)(tonic_sf & 0x0F) << 28)
       | ((uint32_t)(tonic_note & 0x0F) << 24)
       | ((uint32_t)(chord_type & 0xFF) << 16)
       | ((uint32_t)(alt1_type & 0x0F) << 12)
       | ((uint32_t)(alt1_deg & 0x0F) << 8)
       | ((uint32_t)(alt2_type & 0x0F) << 4)
       | (uint32_t)(alt2_deg & 0x0F);
  w[2] = ((uint32_t)(alt3_type & 0x0F) << 28)
       | ((uint32_t)(alt3_deg & 0x0F) << 24)
       | ((uint32_t)(alt4_type & 0x0F) << 20)
       | ((uint32_t)(alt4_deg & 0x0F) << 16);
  w[3] = ((uint32_t)(bass_sf & 0x0F) << 28)
       | ((uint32_t)(bass_note & 0x0F) << 24)
       | ((uint32_t)(bass_type & 0xFF) << 16)
       | ((uint32_t)(bass_alt1_type & 0x0F) << 12)
       | ((uint32_t)(bass_alt1_deg & 0x0F) << 8)
       | ((uint32_t)(bass_alt2_type & 0x0F) << 4)
       | (uint32_t)(bass_alt2_deg & 0x0F);
}

/*--------------------------------------------------------------------+
 * Flex Data Text Messages (MT 0xD, 4 words)
 *
 * Bank 0x01: Metadata Text (project name, composer, copyright, etc.)
 * Bank 0x02: Performance Text (lyrics, ruby, language)
 * UTF-8 text, no BOM. Up to 12 bytes per UMP in words 1-3.
 * Multi-UMP: format 0=complete, 1=start, 2=continue, 3=end.
 *--------------------------------------------------------------------*/

/** @brief Build a Flex Data text message (metadata or performance text).
 *  @param format 0=complete, 1=start, 2=continue, 3=end.
 *  @param address 0=channel, 1=group.
 *  @param bank   0x01=metadata, 0x02=performance text.
 *  @param status Text subtype (see MIDI2_FLEX_TEXT_* / MIDI2_FLEX_PERF_*).
 *  @param text   UTF-8 text data (up to 12 bytes per UMP).
 *  @param len    Bytes of text in this UMP (0-12). */
static inline void midi2_msg_flex_text(uint32_t *w, uint8_t group, uint8_t format,
                                          uint8_t address, uint8_t channel,
                                          uint8_t bank, uint8_t status,
                                          const uint8_t *text, uint8_t len) {
  if (len > 12) len = 12;
  w[0] = midi2_msg_build_flex_w0_full(group, format, address, channel, bank, status);
  w[1] = 0; w[2] = 0; w[3] = 0;
  uint8_t i;
  for (i = 0; i < len; i++) {
    uint8_t wi = (uint8_t)(1 + i / 4);
    uint8_t sh = (uint8_t)(24 - (i % 4) * 8);
    w[wi] |= ((uint32_t)text[i] << sh);
  }
}

/*--------------------------------------------------------------------+
 * SysEx7 Single Packet (MT 0x3, 2 words)
 *
 * Word 0: [MT:4][group:4][status:4][numBytes:4][data0:8][data1:8]
 * Word 1: [data2:8][data3:8][data4:8][data5:8]
 *--------------------------------------------------------------------*/
static inline void midi2_msg_sysex7_packet(uint32_t *w, uint8_t group,
                                             uint8_t status, const uint8_t *data, uint8_t len) {
  if (len > 6) len = 6;
  w[0] = ((uint32_t)MIDI2_MT_SYSEX7 << 28)
       | ((uint32_t)(group & 0x0F) << 24)
       | ((uint32_t)(status | (len & 0x0F)) << 16);
  w[1] = 0;
  uint8_t i;
  for (i = 0; i < len; i++) {
    uint8_t wi = (i < 2) ? 0 : 1;
    uint8_t sh = (i < 2) ? (uint8_t)(8 - i * 8) : (uint8_t)(24 - (i - 2) * 8);
    w[wi] |= ((uint32_t)data[i] << sh);
  }
}

/*--------------------------------------------------------------------+
 * Utility Messages (MT 0x0, 1 word) per M2-104-UM section 7.2
 *
 * Word: [MT:4][reserved:4][status:4][reserved:4][timestamp:16]
 *
 * Utility messages are Groupless in spec v1.1.2; the former Group
 * field (bits [27:24]) is now Reserved and shall be zero.
 *
 * NOOP        status = 0x0, timestamp = 0
 * JR Clock    status = 0x1, timestamp = sender clock time (1/31250 s)
 * JR Timestmp status = 0x2, timestamp = sender clock timestamp
 * DCTPQ       status = 0x3, timestamp = ticks per quarter note (1..65535)
 * Delta Clkst status = 0x4, ticks = 20 bits at the LSB end
 *--------------------------------------------------------------------*/
enum {
  MIDI2_UTILITY_NOOP         = 0x00,
  MIDI2_UTILITY_JR_CLOCK     = 0x01,
  MIDI2_UTILITY_JR_TIMESTAMP = 0x02,
  MIDI2_UTILITY_DCTPQ        = 0x03,  /* Delta Clockstamp Ticks Per Quarter Note */
  MIDI2_UTILITY_DC           = 0x04,  /* Delta Clockstamp (ticks since last event) */
};

/** @brief Build a NOOP Utility message (status 0x0, all-zero payload). */
static inline uint32_t midi2_msg_noop(void) {
  return ((uint32_t)MIDI2_MT_UTILITY << 28);
}

/** @brief Build a JR Clock message (Sender's current clock time).
 *  @param timestamp 16-bit time in 1/31250 of a second ticks (~32 us). */
static inline uint32_t midi2_msg_jr_clock(uint16_t timestamp) {
  return ((uint32_t)MIDI2_MT_UTILITY << 28)
       | ((uint32_t)MIDI2_UTILITY_JR_CLOCK << 20)
       | (uint32_t)timestamp;
}

/** @brief Build a JR Timestamp message (time tag for the following message).
 *  @param timestamp 16-bit time in 1/31250 of a second ticks (~32 us). */
static inline uint32_t midi2_msg_jr_timestamp(uint16_t timestamp) {
  return ((uint32_t)MIDI2_MT_UTILITY << 28)
       | ((uint32_t)MIDI2_UTILITY_JR_TIMESTAMP << 20)
       | (uint32_t)timestamp;
}

/** @brief Build a Delta Clockstamp Ticks Per Quarter Note (DCTPQ) message.
 *  Declares the tick resolution for Delta Clockstamp messages in a MIDI Clip File.
 *  Word: [MT:4][reserved:4][0011:4][reserved:4][tpq:16]
 *  @param tpq Ticks per quarter note (1-65535, 0 = reserved). */
static inline uint32_t midi2_msg_dctpq(uint16_t tpq) {
  return ((uint32_t)MIDI2_MT_UTILITY << 28)
       | ((uint32_t)MIDI2_UTILITY_DCTPQ << 20)
       | (uint32_t)tpq;
}

/** @brief Build a Delta Clockstamp (DC) message.
 *  Declares ticks since last event in a MIDI Clip File.
 *  Word: [MT:4][reserved:4][0100:4][20-bit ticks]
 *  @param ticks Ticks since last event (20-bit, 0-1048575). */
static inline uint32_t midi2_msg_delta_clockstamp(uint32_t ticks) {
  return ((uint32_t)MIDI2_MT_UTILITY << 28)
       | ((uint32_t)MIDI2_UTILITY_DC << 20)
       | (ticks & 0x000FFFFF);
}

/*--------------------------------------------------------------------+
 * Stream Messages (MT 0xF, 4 words)
 *
 * Word 0: [MT:4][format:2][status:10][data:16]
 * Words 1-3: payload (depends on status)
 *
 * format: 0b00 = complete, 0b01 = start, 0b10 = continue, 0b11 = end
 *--------------------------------------------------------------------*/
enum {
  MIDI2_STREAM_ENDPOINT_DISCOVERY  = 0x000,
  MIDI2_STREAM_ENDPOINT_INFO       = 0x001,
  MIDI2_STREAM_DEVICE_IDENTITY     = 0x002,
  MIDI2_STREAM_ENDPOINT_NAME       = 0x003,
  MIDI2_STREAM_PRODUCT_INSTANCE_ID = 0x004,
  MIDI2_STREAM_CONFIG_REQUEST      = 0x005,
  MIDI2_STREAM_CONFIG_NOTIFY       = 0x006,
  MIDI2_STREAM_FB_DISCOVERY        = 0x010,
  MIDI2_STREAM_FB_INFO             = 0x011,
  MIDI2_STREAM_FB_NAME             = 0x012,
  MIDI2_STREAM_START_OF_CLIP       = 0x020,
  MIDI2_STREAM_END_OF_CLIP         = 0x021,
};

/* Internal: build stream word 0 */
static inline uint32_t midi2_msg_build_stream_w0(uint8_t format, uint16_t status) {
  return ((uint32_t)MIDI2_MT_STREAM << 28)
       | ((uint32_t)(format & 0x03) << 26)
       | ((uint32_t)(status & 0x3FF) << 16);
}

/* Endpoint Discovery: request endpoint info from a device
 * ump_ver_major/minor: UMP version we support
 * filter: bitmask of what to request (bit 0=endpoint info, 1=device identity,
 *         2=endpoint name, 3=product instance ID, 4=stream config) */
static inline void midi2_msg_stream_endpoint_discovery(uint32_t *w,
                                                         uint8_t ump_ver_major,
                                                         uint8_t ump_ver_minor,
                                                         uint8_t filter) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_ENDPOINT_DISCOVERY)
       | ((uint32_t)ump_ver_major << 8)
       | (uint32_t)ump_ver_minor;
  w[1] = (uint32_t)filter;
}

/* Endpoint Info Reply
 * static_fb: true if function blocks are static
 * num_fb: number of function blocks (0-31)
 * midi1_proto: supports MIDI 1.0 protocol
 * midi2_proto: supports MIDI 2.0 protocol
 * rx_jr/tx_jr: supports JR timestamps */
static inline void midi2_msg_stream_endpoint_info(uint32_t *w,
                                                    uint8_t ump_ver_major,
                                                    uint8_t ump_ver_minor,
                                                    bool static_fb, uint8_t num_fb,
                                                    bool midi2_proto, bool midi1_proto,
                                                    bool rx_jr, bool tx_jr) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_ENDPOINT_INFO)
       | ((uint32_t)ump_ver_major << 8)
       | (uint32_t)ump_ver_minor;
  w[1] = MIDI2_BIT_IF(static_fb, 31)
       | ((uint32_t)(num_fb & 0x7F) << 24)
       | MIDI2_BIT_IF(midi2_proto, 9)
       | MIDI2_BIT_IF(midi1_proto, 8)
       | MIDI2_BIT_IF(rx_jr, 1)
       | MIDI2_BIT_IF(tx_jr, 0);
}

/* Device Identity Notification */
static inline void midi2_msg_stream_device_identity(uint32_t *w,
                                                      uint32_t manufacturer_id,
                                                      uint16_t family_id,
                                                      uint16_t model_id,
                                                      uint32_t version_id) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_DEVICE_IDENTITY);
  w[1] = (manufacturer_id & 0x00FFFFFF) << 8;
  w[2] = ((uint32_t)family_id << 16) | (uint32_t)model_id;
  w[3] = version_id;
}

/* Stream Configuration Request (status 0x05).
 * Sent host->device to request a protocol / JR Timestamps configuration.
 * protocol: 0x01 = MIDI 1.0, 0x02 = MIDI 2.0.
 * rx_jr_enable: ask the device to accept JR Timestamps inbound.
 * tx_jr_enable: ask the device to emit JR Timestamps outbound. */
static inline void midi2_msg_stream_config_request(uint32_t *w, uint8_t protocol,
                                                     bool rx_jr_enable,
                                                     bool tx_jr_enable) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_CONFIG_REQUEST)
       | ((uint32_t)protocol << 8)
       | (rx_jr_enable ? (UINT32_C(1) << 1) : 0)
       | (tx_jr_enable ? (UINT32_C(1) << 0) : 0);
}

/* Stream Configuration Notification (status 0x06).
 * protocol: 0x01=MIDI1, 0x02=MIDI2.
 * rx_jr_enable: device is currently configured to accept JR Timestamps inbound.
 * tx_jr_enable: device is currently emitting JR Timestamps outbound. */
static inline void midi2_msg_stream_config_notify(uint32_t *w, uint8_t protocol,
                                                    bool rx_jr_enable,
                                                    bool tx_jr_enable) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_CONFIG_NOTIFY)
       | ((uint32_t)protocol << 8)
       | (rx_jr_enable ? (UINT32_C(1) << 1) : 0)
       | (tx_jr_enable ? (UINT32_C(1) << 0) : 0);
}

/* Function Block Discovery
 * fb_num: function block number to query (0xFF = all)
 * filter: bitmask (bit 0 = FB info, bit 1 = FB name) */
static inline void midi2_msg_stream_fb_discovery(uint32_t *w, uint8_t fb_num, uint8_t filter) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_FB_DISCOVERY)
       | ((uint32_t)fb_num << 8)
       | (uint32_t)filter;
}

/* Function Block Info
 * active: FB is active
 * fb_num: function block number
 * direction: 0x00=reserved, 0x01=Receiver, 0x02=Sender, 0x03=Bidirectional
 * ui_hint: 0x00=Undeclared, 0x01=Receiver, 0x02=Sender, 0x03=Sender+Receiver
 * first_group: first group in this FB
 * num_groups: number of groups
 * midi_ci_ver: MIDI-CI version support (0=none, 1=1.1, 2=1.2)
 * max_sysex8_streams: 0 = SysEx8 not supported, 1..63 = max concurrent streams
 * protocol: 0x00=unknown, 0x01=MIDI1, 0x02=MIDI2, 0x03=both */
static inline void midi2_msg_stream_fb_info(uint32_t *w,
                                              bool active, uint8_t fb_num,
                                              uint8_t direction, uint8_t ui_hint,
                                              uint8_t first_group, uint8_t num_groups,
                                              uint8_t midi_ci_ver,
                                              uint8_t max_sysex8_streams,
                                              uint8_t protocol) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_FB_INFO)
       | (active ? (UINT32_C(1) << 15) : 0)
       | ((uint32_t)(fb_num & 0x7F) << 8)
       | ((uint32_t)(ui_hint & 0x03) << 4)
       | (uint32_t)(direction & 0x03);
  w[1] = ((uint32_t)(first_group & 0x0F) << 24)
       | ((uint32_t)(num_groups & 0x0F) << 16)
       | ((uint32_t)(midi_ci_ver & 0x03) << 8)
       | ((uint32_t)(max_sysex8_streams & 0x3F) << 2)
       | (uint32_t)(protocol & 0x03);
}

/* Endpoint Name Notification (multi-packet text, up to 14 bytes per UMP).
 * format: 0=complete, 1=start, 2=continue, 3=end.
 * name: UTF-8 text, up to 14 bytes per UMP. */
static inline void midi2_msg_stream_endpoint_name(uint32_t *w, uint8_t format,
                                                     const uint8_t *name, uint8_t len) {
  if (len > 14) len = 14;
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(format, MIDI2_STREAM_ENDPOINT_NAME);
  uint8_t i;
  for (i = 0; i < len; i++) {
    if (i < 2) {
      w[0] |= ((uint32_t)name[i] << (8 - i * 8));
    } else {
      uint8_t offset = (uint8_t)(i - 2);
      uint8_t dwi = (uint8_t)(1 + offset / 4);
      uint8_t dsh = (uint8_t)(24 - (offset % 4) * 8);
      w[dwi] |= ((uint32_t)name[i] << dsh);
    }
  }
}

/* Product Instance Id Notification (multi-packet text, up to 14 bytes per UMP). */
static inline void midi2_msg_stream_product_id(uint32_t *w, uint8_t format,
                                                  const uint8_t *id, uint8_t len) {
  if (len > 14) len = 14;
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(format, MIDI2_STREAM_PRODUCT_INSTANCE_ID);
  uint8_t i;
  for (i = 0; i < len; i++) {
    if (i < 2) {
      w[0] |= ((uint32_t)id[i] << (8 - i * 8));
    } else {
      uint8_t offset = (uint8_t)(i - 2);
      uint8_t dwi = (uint8_t)(1 + offset / 4);
      uint8_t dsh = (uint8_t)(24 - (offset % 4) * 8);
      w[dwi] |= ((uint32_t)id[i] << dsh);
    }
  }
}

/* Function Block Name Notification (multi-packet text, up to 13 bytes per UMP).
 * fb_num goes in w[0] bits [15:8], leaving 13 bytes for name. */
static inline void midi2_msg_stream_fb_name(uint32_t *w, uint8_t format,
                                               uint8_t fb_num,
                                               const uint8_t *name, uint8_t len) {
  if (len > 13) len = 13;
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(format, MIDI2_STREAM_FB_NAME)
       | ((uint32_t)fb_num << 8);
  uint8_t i;
  for (i = 0; i < len; i++) {
    if (i == 0) {
      w[0] |= (uint32_t)name[0];
    } else {
      uint8_t offset = (uint8_t)(i - 1);
      uint8_t dwi = (uint8_t)(1 + offset / 4);
      uint8_t dsh = (uint8_t)(24 - (offset % 4) * 8);
      w[dwi] |= ((uint32_t)name[i] << dsh);
    }
  }
}

/* Start/End of Clip */
static inline void midi2_msg_stream_start_of_clip(uint32_t *w) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_START_OF_CLIP);
}

static inline void midi2_msg_stream_end_of_clip(uint32_t *w) {
  memset(w, 0, 16);
  w[0] = midi2_msg_build_stream_w0(0, MIDI2_STREAM_END_OF_CLIP);
}

/*--------------------------------------------------------------------+
 * SysEx8 (MT 0x5, 4 words)
 *
 * Word 0: [MT:4][group:4][status:4][numBytes:4][streamID:8][data0:8]
 * Word 1: [data1:8][data2:8][data3:8][data4:8]
 * Word 2: [data5:8][data6:8][data7:8][data8:8]
 * Word 3: [data9:8][data10:8][data11:8][data12:8]
 *
 * Max 13 data bytes per packet. 8-bit data (no 7-bit restriction).
 * SysEx8 and SysEx7 coexist in the same UMP stream as different MT values.
 *--------------------------------------------------------------------*/
enum {
  MIDI2_SYSEX8_COMPLETE = 0x00,
  MIDI2_SYSEX8_START    = 0x10,
  MIDI2_SYSEX8_CONTINUE = 0x20,
  MIDI2_SYSEX8_END      = 0x30,
};

static inline void midi2_msg_sysex8_packet(uint32_t *w, uint8_t group,
                                             uint8_t status, uint8_t stream_id,
                                             const uint8_t *data, uint8_t len) {
  if (len > 13) len = 13;
  memset(w, 0, 16);
  /* numBytes includes stream_id + data bytes */
  uint8_t num_bytes = (uint8_t)(len + 1);
  w[0] = ((uint32_t)MIDI2_MT_DATA128 << 28)
       | ((uint32_t)(group & 0x0F) << 24)
       | ((uint32_t)(status | (num_bytes & 0x0F)) << 16)
       | ((uint32_t)stream_id << 8);

  /* Pack data bytes: data[0] goes to w[0] bits [7:0],
   * data[1..4] into w[1], data[5..8] into w[2], data[9..12] into w[3] */
  uint8_t i;
  if (len >= 1) w[0] |= (uint32_t)data[0];
  for (i = 1; i < len; i++) {
    uint8_t wi = (uint8_t)(1 + (i - 1) / 4);
    uint8_t sh = (uint8_t)(24 - ((i - 1) % 4) * 8);
    w[wi] |= ((uint32_t)data[i] << sh);
  }
}

/*--------------------------------------------------------------------+
 * Mixed Data Set (MT 0x5, status 0x8/0x9, 4 words)
 *
 * MDS carries non-MIDI payloads (firmware, XML, etc.) in chunks.
 * Each chunk: 1 Header UMP + N Payload UMPs, all sharing mds_id.
 * Cannot be translated to MIDI 1.0.
 *--------------------------------------------------------------------*/
enum {
  MIDI2_MDS_HEADER  = 0x80,
  MIDI2_MDS_PAYLOAD = 0x90,
};

/** @brief Build a Mixed Data Set Header UMP.
 *  @param mds_id    MDS ID (0-15), ties chunks together.
 *  @param num_bytes Number of valid bytes in this chunk (including header).
 *  @param num_chunks Total chunks in data set (0 = unknown).
 *  @param this_chunk This chunk number (starting from 1).
 *  @param mfr_id    16-bit Manufacturer ID (see spec 7.10).
 *  @param device_id Device ID (0xFFFF = all call).
 *  @param sub_id1   Sub ID #1.
 *  @param sub_id2   Sub ID #2. */
static inline void midi2_msg_mds_header(uint32_t *w, uint8_t group, uint8_t mds_id,
                                          uint16_t num_bytes, uint16_t num_chunks,
                                          uint16_t this_chunk, uint16_t mfr_id,
                                          uint16_t device_id, uint16_t sub_id1,
                                          uint16_t sub_id2) {
  w[0] = ((uint32_t)MIDI2_MT_DATA128 << 28)
       | ((uint32_t)(group & 0x0F) << 24)
       | ((uint32_t)(MIDI2_MDS_HEADER | (mds_id & 0x0F)) << 16)
       | (uint32_t)num_bytes;
  w[1] = ((uint32_t)num_chunks << 16) | (uint32_t)this_chunk;
  w[2] = ((uint32_t)mfr_id << 16) | (uint32_t)device_id;
  w[3] = ((uint32_t)sub_id1 << 16) | (uint32_t)sub_id2;
}

/** @brief Build a Mixed Data Set Payload UMP.
 *  @param mds_id MDS ID (0-15).
 *  @param data   Payload bytes (up to 14).
 *  @param len    Number of payload bytes (0-14). */
static inline void midi2_msg_mds_payload(uint32_t *w, uint8_t group, uint8_t mds_id,
                                            const uint8_t *data, uint8_t len) {
  if (len > 14) len = 14;
  memset(w, 0, 16);
  w[0] = ((uint32_t)MIDI2_MT_DATA128 << 28)
       | ((uint32_t)(group & 0x0F) << 24)
       | ((uint32_t)(MIDI2_MDS_PAYLOAD | (mds_id & 0x0F)) << 16);
  /* Pack 14 data bytes into w[0] bits [15:0] + w[1..3] */
  uint8_t i;
  for (i = 0; i < len; i++) {
    uint8_t wi = (uint8_t)((i + 2) / 4);
    uint8_t sh = (uint8_t)(24 - ((i + 2) % 4) * 8);
    w[wi] |= ((uint32_t)data[i] << sh);
  }
}

/*--------------------------------------------------------------------+
 * MIDI 1.0 Byte Stream to UMP Conversion (stateless, single message)
 *
 * Converts a complete MIDI 1.0 message (1-3 bytes) to a UMP word (MT 0x2).
 * Does NOT handle Running Status or SysEx -- that requires state (see midi2_conv).
 * Useful when the platform already parsed the byte stream.
 *--------------------------------------------------------------------*/
static inline uint32_t midi2_msg_from_midi1(uint8_t group,
                                              uint8_t status, uint8_t data1, uint8_t data2) {
  return ((uint32_t)MIDI2_MT_MIDI1_CV << 28)
       | ((uint32_t)(group & 0x0F) << 24)
       | ((uint32_t)status << 16)
       | ((uint32_t)(data1 & 0x7F) << 8)
       | (uint32_t)(data2 & 0x7F);
}

/*--------------------------------------------------------------------+
 * Protocol Translation: MT 0x2 (MIDI 1.0 CV) -> MT 0x4 (MIDI 2.0 CV)
 *
 * Translates a 1-word MT 0x2 message to a 2-word MT 0x4 message with
 * proper value scaling per M2-104-UM v1.1.2, Section 7.
 *
 * Special cases:
 *   - Note On with velocity 0 becomes Note Off (velocity 0x8000)
 *   - Program Change: bank_valid = false (no bank info in MT 0x2)
 *   - Pitch Bend: combines data1 (LSB) + data2 (MSB) before scaling
 *
 * Returns true if the message was translated, false if mt2_word is not
 * a Channel Voice message (wrong MT or unrecognized status), or if out
 * is NULL. Safe to call with out == NULL; returns false without side
 * effects.
 *--------------------------------------------------------------------*/
static inline bool midi2_msg_mt2_to_mt4(uint32_t mt2_word, uint32_t out[2]) {
  if (out == NULL) return false;
  if (midi2_msg_get_mt(&mt2_word) != MIDI2_MT_MIDI1_CV) return false;

  uint8_t group   = (mt2_word >> 24) & 0x0F;
  uint8_t status  = (mt2_word >> 16) & 0xF0;
  uint8_t channel = (mt2_word >> 16) & 0x0F;
  uint8_t data1   = (mt2_word >>  8) & 0x7F;
  uint8_t data2   = (mt2_word      ) & 0x7F;

  switch (status) {
    case 0x90: /* Note On */
      if (data2 == 0) {
        /* velocity 0 means Note Off per MIDI 1.0 convention */
        midi2_msg_note_off(out, group, channel, data1,
                           midi2_msg_scale_up_7to16(64), 0, 0);
      } else {
        midi2_msg_note_on(out, group, channel, data1,
                          midi2_msg_scale_up_7to16(data2), 0, 0);
      }
      return true;

    case 0x80: /* Note Off */
      midi2_msg_note_off(out, group, channel, data1,
                         midi2_msg_scale_up_7to16(data2), 0, 0);
      return true;

    case 0xB0: /* Control Change */
      midi2_msg_cc(out, group, channel, data1,
                   midi2_msg_scale_up_7to32(data2));
      return true;

    case 0xC0: /* Program Change */
      midi2_msg_program(out, group, channel, data1, false, 0, 0);
      return true;

    case 0xD0: /* Channel Pressure */
      midi2_msg_chan_pressure(out, group, channel,
                              midi2_msg_scale_up_7to32(data1));
      return true;

    case 0xE0: /* Pitch Bend */
      {
        uint16_t bend14 = ((uint16_t)data2 << 7) | data1;
        midi2_msg_pitch_bend(out, group, channel,
                             midi2_msg_scale_up_14to32(bend14));
      }
      return true;

    case 0xA0: /* Poly Pressure */
      midi2_msg_poly_pressure(out, group, channel, data1,
                               midi2_msg_scale_up_7to32(data2));
      return true;

    default:
      return false;
  }
}

/*--------------------------------------------------------------------+
 * Protocol Translation: MT 0x4 (MIDI 2.0 CV) -> MT 0x2 (MIDI 1.0 CV)
 *
 * Inverse of midi2_msg_mt2_to_mt4. Lossy by spec: MIDI 1.0 CV cannot
 * carry RPN/NRPN/Rel/Per-Note in a single word (would require a 4-CC
 * sequence). Those statuses are skipped (returns 0 words emitted).
 * Caller detects skips by comparing emitted word count against the
 * expected count (1 word per MT 0x4 message that is supported).
 *
 * Mapping per M2-115 section 4.2 / 4.3:
 *   Note On/Off       : velocity 16-bit -> 7-bit
 *   CC                : value    32-bit -> 7-bit
 *   Pitch Bend        :          32-bit -> 14-bit (LSB / MSB split)
 *   Channel Pressure  :          32-bit -> 7-bit
 *   Poly Pressure     :          32-bit -> 7-bit
 *   Program Change    : program byte preserved; bank dropped
 *   Per-Note CC/PB/Mgmt, RPN/NRPN/Rel: dropped (no MIDI 1.0 form)
 *
 *  @return number of MT 0x2 words written (0 or 1). Returns 0 if either
 *          mt4_words or out_word is NULL.
 *  (v0.3.0+) */
static inline uint32_t midi2_msg_mt4_to_mt2(const uint32_t mt4_words[2],
                                             uint32_t *out_word) {
  if (mt4_words == NULL || out_word == NULL) return 0;
  uint8_t mt = (uint8_t)((mt4_words[0] >> 28) & 0x0Fu);
  if (mt != MIDI2_MT_MIDI2_CV) return 0;
  uint8_t grp  = (uint8_t)((mt4_words[0] >> 24) & 0x0Fu);
  uint8_t stat = (uint8_t)((mt4_words[0] >> 16) & 0xFFu);
  uint8_t hi   = (uint8_t)(stat & 0xF0u);
  uint8_t ch   = (uint8_t)(stat & 0x0Fu);

  switch (hi) {
    case MIDI2_STATUS_NOTE_OFF:
    case MIDI2_STATUS_NOTE_ON: {
      uint8_t  note  = (uint8_t)((mt4_words[0] >> 8) & 0x7Fu);
      uint16_t vel16 = (uint16_t)((mt4_words[1] >> 16) & 0xFFFFu);
      uint8_t  vel7  = midi2_msg_scale_down_16to7(vel16);
      *out_word = ((uint32_t)MIDI2_MT_MIDI1_CV << 28)
                | ((uint32_t)grp << 24)
                | ((uint32_t)hi  << 16)
                | ((uint32_t)ch  << 16)
                | ((uint32_t)note << 8)
                | (uint32_t)vel7;
      return 1;
    }
    case MIDI2_STATUS_POLY_PRESSURE: {
      uint8_t note = (uint8_t)((mt4_words[0] >> 8) & 0x7Fu);
      uint8_t v7   = midi2_msg_scale_down_32to7(mt4_words[1]);
      *out_word = ((uint32_t)MIDI2_MT_MIDI1_CV << 28)
                | ((uint32_t)grp << 24)
                | ((uint32_t)MIDI2_STATUS_POLY_PRESSURE << 16)
                | ((uint32_t)ch << 16)
                | ((uint32_t)note << 8)
                | (uint32_t)v7;
      return 1;
    }
    case MIDI2_STATUS_CC: {
      uint8_t cc = (uint8_t)((mt4_words[0] >> 8) & 0x7Fu);
      uint8_t v7 = midi2_msg_scale_down_32to7(mt4_words[1]);
      *out_word = ((uint32_t)MIDI2_MT_MIDI1_CV << 28)
                | ((uint32_t)grp << 24)
                | ((uint32_t)MIDI2_STATUS_CC << 16)
                | ((uint32_t)ch << 16)
                | ((uint32_t)cc << 8)
                | (uint32_t)v7;
      return 1;
    }
    case MIDI2_STATUS_PROGRAM: {
      uint8_t prog = (uint8_t)((mt4_words[1] >> 24) & 0x7Fu);
      *out_word = ((uint32_t)MIDI2_MT_MIDI1_CV << 28)
                | ((uint32_t)grp << 24)
                | ((uint32_t)MIDI2_STATUS_PROGRAM << 16)
                | ((uint32_t)ch << 16)
                | ((uint32_t)prog << 8);
      return 1;
    }
    case MIDI2_STATUS_CHAN_PRESSURE: {
      uint8_t v7 = midi2_msg_scale_down_32to7(mt4_words[1]);
      *out_word = ((uint32_t)MIDI2_MT_MIDI1_CV << 28)
                | ((uint32_t)grp << 24)
                | ((uint32_t)MIDI2_STATUS_CHAN_PRESSURE << 16)
                | ((uint32_t)ch << 16)
                | ((uint32_t)v7 << 8);
      return 1;
    }
    case MIDI2_STATUS_PITCH_BEND: {
      uint16_t pb14 = midi2_msg_scale_down_32to14(mt4_words[1]);
      *out_word = ((uint32_t)MIDI2_MT_MIDI1_CV << 28)
                | ((uint32_t)grp << 24)
                | ((uint32_t)MIDI2_STATUS_PITCH_BEND << 16)
                | ((uint32_t)ch << 16)
                | ((uint32_t)(pb14 & 0x7Fu) << 8)
                | (uint32_t)((pb14 >> 7) & 0x7Fu);
      return 1;
    }
    default:
      /* RPN/NRPN/Rel/Per-Note dropped; caller detects via count. */
      return 0;
  }
}

/*--------------------------------------------------------------------+
 * USB MIDI 1.0 cable event -> UMP MT 0x2
 *
 * USB MIDI v1.0 class delivers Channel Voice and System Common
 * messages as 4-byte cable events:
 *   byte 0 = (cable_number << 4) | CIN
 *   byte 1 = MIDI status byte
 *   byte 2 = data 1
 *   byte 3 = data 2
 * Packed LSB-first into the uint32_t argument.
 *
 * Supported CINs: 0x2, 0x3 (System Common), 0x8-0xE (Channel Voice).
 * Reserved CINs (0x0, 0x1) and SysEx fragments (0x4-0x7, 0xF) return
 * false; the latter need stateful reassembly handled by midi2_conv.
 *
 *  @return true on success, false on unsupported CIN or NULL output.
 *  (v0.3.0+) */
static inline bool midi2_msg_cable_event_to_ump(uint32_t cable_event,
                                                 uint8_t group,
                                                 uint32_t *ump_out) {
  if (ump_out == NULL) return false;
  uint8_t b0     = (uint8_t)(cable_event        & 0xFFu);
  uint8_t status = (uint8_t)((cable_event >>  8) & 0xFFu);
  uint8_t data1  = (uint8_t)((cable_event >> 16) & 0xFFu);
  uint8_t data2  = (uint8_t)((cable_event >> 24) & 0xFFu);
  uint8_t cin    = (uint8_t)(b0 & 0x0Fu);

  /* USB MIDI 1.0 class spec table 4-1: CINs that map to Channel Voice
   * or 2/3-byte System Common UMP MT 0x2. Other CINs are reserved
   * (0x0, 0x1), SysEx fragments handled by midi2_conv (0x4-0x7), or
   * single-byte real-time (0xF). */
  /* Positional initializer (index 0x0..0xF). C99 and C++ both accept it;
   * designated array initializers do not compile under C++. */
  static const uint8_t cin_to_cv[16] = {
    0, 0, 1, 1,  /* 0x0/0x1 reserved, 0x2/0x3 System Common 2/3-byte */
    0, 0, 0, 0,  /* 0x4-0x7 SysEx fragments (midi2_conv) */
    1, 1, 1, 1,  /* 0x8 Note Off, 0x9 Note On, 0xA PolyAT, 0xB CC */
    1, 1, 1, 0,  /* 0xC Program, 0xD ChanAT, 0xE PB, 0xF single-byte RT */
  };

  if (cin_to_cv[cin] == 0u) return false;

  *ump_out = ((uint32_t)MIDI2_MT_MIDI1_CV << 28)
           | ((uint32_t)(group & 0x0Fu) << 24)
           | ((uint32_t)status << 16)
           | ((uint32_t)(data1 & 0x7Fu) << 8)
           | ((uint32_t)(data2 & 0x7Fu));
  return true;
}

/* == midi2_ci_msg ======================================================== */


/*
 * midi2_ci_msg.h - MIDI-CI message construction and parsing
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI-CI (M2-101-UM v1.2, Jun 2023)
 */




/*====================================================================+
 * midi2_ci_msg -- MIDI-CI message construction and parsing
 *
 * Header-only, stateless. Builds and reads MIDI-CI SysEx payloads
 * per M2-101-UM MIDI-CI Specification v1.2.
 *
 * All build functions write into a caller-provided buffer and return
 * the number of bytes written. The buffer does NOT include F0/F7
 * delimiters (those are added by the SysEx transport layer).
 *
 * All parse functions extract fields from a received SysEx payload
 * (also without F0/F7).
 *====================================================================*/

/*--------------------------------------------------------------------+
 * Constants
 *--------------------------------------------------------------------*/

#define MIDI2_CI_BROADCAST_MUID  UINT32_C(0x0FFFFFFF)
#define MIDI2_CI_VERSION_1       0x01
#define MIDI2_CI_VERSION_2       0x02  /* current: MIDI-CI v1.2 */

/*--------------------------------------------------------------------+
 * Sub-ID#2 values (Appendix D, M2-101-UM)
 *--------------------------------------------------------------------*/
enum {
  /* Category 7: Management (0x70-0x7F) */
  MIDI2_CI_DISCOVERY             = 0x70,
  MIDI2_CI_DISCOVERY_REPLY       = 0x71,
  MIDI2_CI_ENDPOINT_INFO         = 0x72,
  MIDI2_CI_ENDPOINT_INFO_REPLY   = 0x73,
  MIDI2_CI_ACK                   = 0x7D,
  MIDI2_CI_INVALIDATE_MUID       = 0x7E,
  MIDI2_CI_NAK                   = 0x7F,

  /* Category 2: Profile Configuration (0x20-0x2F) */
  MIDI2_CI_PROFILE_INQUIRY       = 0x20,
  MIDI2_CI_PROFILE_INQUIRY_REPLY = 0x21,
  MIDI2_CI_SET_PROFILE_ON        = 0x22,
  MIDI2_CI_SET_PROFILE_OFF       = 0x23,
  MIDI2_CI_PROFILE_ENABLED       = 0x24,
  MIDI2_CI_PROFILE_DISABLED      = 0x25,
  MIDI2_CI_PROFILE_ADDED         = 0x26,
  MIDI2_CI_PROFILE_REMOVED       = 0x27,
  MIDI2_CI_PROFILE_DETAILS       = 0x28,
  MIDI2_CI_PROFILE_DETAILS_REPLY = 0x29,
  MIDI2_CI_PROFILE_SPECIFIC_DATA = 0x2F,

  /* Category 3: Property Exchange (0x30-0x3F) */
  MIDI2_CI_PE_CAPABILITY         = 0x30,
  MIDI2_CI_PE_CAPABILITY_REPLY   = 0x31,
  MIDI2_CI_PE_GET                = 0x34,
  MIDI2_CI_PE_GET_REPLY          = 0x35,
  MIDI2_CI_PE_SET                = 0x36,
  MIDI2_CI_PE_SET_REPLY          = 0x37,
  MIDI2_CI_PE_SUBSCRIBE          = 0x38,
  MIDI2_CI_PE_SUBSCRIBE_REPLY    = 0x39,
  MIDI2_CI_PE_NOTIFY             = 0x3F,

  /* Category 4: Process Inquiry (0x40-0x4F) */
  MIDI2_CI_PI_CAPABILITY         = 0x40,
  MIDI2_CI_PI_CAPABILITY_REPLY   = 0x41,
  MIDI2_CI_PI_MIDI_REPORT        = 0x42,
  MIDI2_CI_PI_MIDI_REPORT_REPLY  = 0x43,
  MIDI2_CI_PI_MIDI_REPORT_END    = 0x44,
};

/*--------------------------------------------------------------------+
 * Capability Inquiry Category Supported bitmap (Table 7)
 *--------------------------------------------------------------------*/
enum {
  MIDI2_CI_CAT_PROFILE_CONFIG    = 0x04,  /* bit 2 */
  MIDI2_CI_CAT_PROPERTY_EXCHANGE = 0x08,  /* bit 3 */
  MIDI2_CI_CAT_PROCESS_INQUIRY   = 0x10,  /* bit 4 */
};

/*--------------------------------------------------------------------+
 * NAK Status Codes (Table 16)
 *--------------------------------------------------------------------*/
enum {
  MIDI2_CI_NAK_OK                = 0x00,
  MIDI2_CI_NAK_NOT_SUPPORTED     = 0x01,
  MIDI2_CI_NAK_VERSION_ERR       = 0x02,
  MIDI2_CI_NAK_CH_NOT_IN_USE     = 0x03,
  MIDI2_CI_NAK_PROFILE_NOT_SUPP  = 0x04,
  MIDI2_CI_NAK_TERMINATE_PE      = 0x20,
  MIDI2_CI_NAK_PE_OUT_OF_SEQ     = 0x21,
  MIDI2_CI_NAK_ERROR_RETRY       = 0x40,
  MIDI2_CI_NAK_MALFORMED         = 0x41,
  MIDI2_CI_NAK_TIMEOUT           = 0x42,
  MIDI2_CI_NAK_BUSY              = 0x43,
};

/*--------------------------------------------------------------------+
 * ACK Status Codes (Table 14)
 *--------------------------------------------------------------------*/
enum {
  MIDI2_CI_ACK_OK                = 0x00,
  MIDI2_CI_ACK_TIMEOUT_WAIT      = 0x10,
};

/*--------------------------------------------------------------------+
 * MUID utilities
 *--------------------------------------------------------------------*/

/** Read 28-bit MUID from 4 bytes (LSB first, 7-bit encoding). */
static inline uint32_t midi2_ci_read_muid(const uint8_t *p) {
  return (uint32_t)(p[0] & 0x7F)
       | ((uint32_t)(p[1] & 0x7F) << 7)
       | ((uint32_t)(p[2] & 0x7F) << 14)
       | ((uint32_t)(p[3] & 0x7F) << 21);
}

/** Write 28-bit MUID as 4 bytes (LSB first, 7-bit encoding). */
static inline void midi2_ci_write_muid(uint8_t *p, uint32_t muid) {
  p[0] = (uint8_t)((muid >> 0) & 0x7F);
  p[1] = (uint8_t)((muid >> 7) & 0x7F);
  p[2] = (uint8_t)((muid >> 14) & 0x7F);
  p[3] = (uint8_t)((muid >> 21) & 0x7F);
}

/*--------------------------------------------------------------------+
 * Common header: parse
 *
 * Standard CI SysEx layout (without F0/F7):
 *   [0]  0x7E  Universal System Exclusive
 *   [1]  Device ID (0x00-0x0F = channel, 0x7E = group, 0x7F = function block)
 *   [2]  0x0D  MIDI-CI Sub-ID#1
 *   [3]  Sub-ID#2 (message type)
 *   [4]  MIDI-CI Message Version/Format
 *   [5..8]   Source MUID (4 bytes, LSB first)
 *   [9..12]  Destination MUID (4 bytes, LSB first)
 *   [13..]   Message-specific data
 *--------------------------------------------------------------------*/

static inline uint8_t  midi2_ci_get_device_id(const uint8_t *d)    { return d[1]; }
static inline uint8_t  midi2_ci_get_sub_id(const uint8_t *d)       { return d[3]; }
static inline uint8_t  midi2_ci_get_version(const uint8_t *d)      { return d[4]; }
static inline uint32_t midi2_ci_get_src_muid(const uint8_t *d)     { return midi2_ci_read_muid(&d[5]); }
static inline uint32_t midi2_ci_get_dst_muid(const uint8_t *d)     { return midi2_ci_read_muid(&d[9]); }

/** Check if SysEx payload is a MIDI-CI message (7E xx 0D ...).
 *  Safe to call with NULL d (returns false). */
static inline bool midi2_ci_is_ci(const uint8_t *d, uint16_t len) {
  return d != NULL && len >= 13 && d[0] == 0x7E && d[2] == 0x0D;
}

/*--------------------------------------------------------------------+
 * Common header: build
 *
 * Returns offset after header (13 bytes). Caller continues writing
 * message-specific data at the returned offset.
 *--------------------------------------------------------------------*/

static inline uint16_t midi2_ci_build_header(uint8_t *buf, uint8_t device_id,
                                                uint8_t sub_id, uint8_t version,
                                                uint32_t src_muid, uint32_t dst_muid) {
  buf[0] = 0x7E;
  buf[1] = device_id;
  buf[2] = 0x0D;
  buf[3] = sub_id;
  buf[4] = version;
  midi2_ci_write_muid(&buf[5], src_muid);
  midi2_ci_write_muid(&buf[9], dst_muid);
  return 13;
}

/*--------------------------------------------------------------------+
 * 14-bit value read/write (2 bytes, LSB first, 7-bit per byte)
 *
 * All multi-byte fields inside SysEx use 7-bit encoding (bit 7 = 0).
 * Range: 0 to 16383 (0x3FFF). Used for profile counts, data lengths,
 * chunk numbers, and channel counts in MIDI-CI messages.
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_read_14(const uint8_t *p) {
  return (uint16_t)(p[0] & 0x7F) | ((uint16_t)(p[1] & 0x7F) << 7);
}
static inline void midi2_ci_write_14(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0x7F);
  p[1] = (uint8_t)((v >> 7) & 0x7F);
}

/*--------------------------------------------------------------------+
 * 28-bit value read/write (4 bytes, LSB first, 7-bit each)
 *--------------------------------------------------------------------*/
static inline uint32_t midi2_ci_read_28(const uint8_t *p) {
  return midi2_ci_read_muid(p); /* same encoding */
}
static inline void midi2_ci_write_28(uint8_t *p, uint32_t v) {
  midi2_ci_write_muid(p, v);
}

/*--------------------------------------------------------------------+
 * Parse helpers for common message fields
 *
 * All offsets assume standard header (13 bytes). Message-specific data
 * starts at byte 13. These extract fields without the caller needing
 * to know byte offsets.
 *--------------------------------------------------------------------*/

/* Discovery / Discovery Reply: device identification fields at offset 13.
 * Manufacturer ID: 3 bytes, literal SysEx ID bytes (all <= 0x7F).
 * Family/Model: 2 bytes each, 7-bit LSB-first (14-bit value).
 * SW Revision: 4 bytes, 7-bit LSB-first (28-bit value).
 * These match the "Device Inquiry" Universal SysEx format (section 5.5.1). */
static inline uint32_t midi2_ci_get_mfr_id(const uint8_t *d) {
  return (uint32_t)d[13] | ((uint32_t)d[14] << 8) | ((uint32_t)d[15] << 16);
}
static inline uint16_t midi2_ci_get_family(const uint8_t *d) {
  return midi2_ci_read_14(&d[16]);
}
static inline uint16_t midi2_ci_get_model(const uint8_t *d) {
  return midi2_ci_read_14(&d[18]);
}
static inline uint32_t midi2_ci_get_sw_rev(const uint8_t *d) {
  return midi2_ci_read_28(&d[20]);
}
static inline uint8_t midi2_ci_get_ci_category(const uint8_t *d) {
  return d[24];
}
static inline uint32_t midi2_ci_get_max_sysex(const uint8_t *d) {
  return midi2_ci_read_28(&d[25]);
}

/* Invalidate MUID: target MUID at offset 13 */
static inline uint32_t midi2_ci_get_target_muid(const uint8_t *d) {
  return midi2_ci_read_muid(&d[13]);
}

/* ACK/NAK (v2+): fields after header */
static inline uint8_t midi2_ci_get_orig_sub_id(const uint8_t *d) { return d[13]; }
static inline uint8_t midi2_ci_get_nak_status_code(const uint8_t *d) { return d[14]; }
static inline uint8_t midi2_ci_get_nak_status_data(const uint8_t *d) { return d[15]; }

/* Profile messages: profile ID at offset 13 (after header) */
static inline const uint8_t *midi2_ci_get_profile_id(const uint8_t *d) { return &d[13]; }

/* Profile Inquiry Reply: counts */
static inline uint16_t midi2_ci_get_enabled_count(const uint8_t *d) {
  return midi2_ci_read_14(&d[13]);
}

/* PE data messages: request_id at offset 13 */
static inline uint8_t midi2_ci_get_pe_request_id(const uint8_t *d) { return d[13]; }
static inline uint16_t midi2_ci_get_pe_header_len(const uint8_t *d) {
  return midi2_ci_read_14(&d[14]);
}

/* Process Inquiry MIDI Report: bitmaps */
static inline uint8_t midi2_ci_get_pi_msg_data_control(const uint8_t *d) { return d[13]; }
static inline uint8_t midi2_ci_get_pi_system_bitmap(const uint8_t *d) { return d[14]; }
static inline uint8_t midi2_ci_get_pi_channel_ctrl_bitmap(const uint8_t *d) { return d[16]; }
static inline uint8_t midi2_ci_get_pi_note_data_bitmap(const uint8_t *d) { return d[17]; }

/*====================================================================+
 * CATEGORY 7: MANAGEMENT MESSAGES
 *====================================================================*/

/*--------------------------------------------------------------------+
 * Discovery (0x70) -- Table 6
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_discovery(
    uint8_t *buf, uint8_t version, uint32_t src_muid,
    uint32_t mfr_id, uint16_t family, uint16_t model, uint32_t sw_rev,
    uint8_t ci_category, uint32_t max_sysex, uint8_t output_path_id) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, MIDI2_CI_DISCOVERY, version,
                                        src_muid, MIDI2_CI_BROADCAST_MUID);
  /* Device Manufacturer (3 bytes SysEx ID) */
  buf[p++] = (uint8_t)((mfr_id >> 0) & 0x7F);
  buf[p++] = (uint8_t)((mfr_id >> 8) & 0x7F);
  buf[p++] = (uint8_t)((mfr_id >> 16) & 0x7F);
  /* Device Family (2 bytes LSB first) */
  buf[p++] = (uint8_t)(family & 0x7F);
  buf[p++] = (uint8_t)((family >> 7) & 0x7F);
  /* Model Number (2 bytes LSB first) */
  buf[p++] = (uint8_t)(model & 0x7F);
  buf[p++] = (uint8_t)((model >> 7) & 0x7F);
  /* Software Revision (4 bytes) */
  midi2_ci_write_28(&buf[p], sw_rev); p += 4;
  /* Capability Inquiry Category Supported */
  buf[p++] = ci_category;
  /* Receivable Maximum SysEx Size (4 bytes LSB first) */
  midi2_ci_write_28(&buf[p], max_sysex); p += 4;
  /* Output Path Id (v2+) */
  if (version >= MIDI2_CI_VERSION_2) {
    buf[p++] = output_path_id;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * Reply to Discovery (0x71) -- Table 8
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_discovery_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint32_t mfr_id, uint16_t family, uint16_t model, uint32_t sw_rev,
    uint8_t ci_category, uint32_t max_sysex,
    uint8_t output_path_id, uint8_t function_block) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, MIDI2_CI_DISCOVERY_REPLY, version,
                                        src_muid, dst_muid);
  buf[p++] = (uint8_t)((mfr_id >> 0) & 0x7F);
  buf[p++] = (uint8_t)((mfr_id >> 8) & 0x7F);
  buf[p++] = (uint8_t)((mfr_id >> 16) & 0x7F);
  buf[p++] = (uint8_t)(family & 0x7F);
  buf[p++] = (uint8_t)((family >> 7) & 0x7F);
  buf[p++] = (uint8_t)(model & 0x7F);
  buf[p++] = (uint8_t)((model >> 7) & 0x7F);
  midi2_ci_write_28(&buf[p], sw_rev); p += 4;
  buf[p++] = ci_category;
  midi2_ci_write_28(&buf[p], max_sysex); p += 4;
  if (version >= MIDI2_CI_VERSION_2) {
    buf[p++] = output_path_id;
    buf[p++] = function_block;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * Inquiry: Endpoint Information (0x72) -- Table 9
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_endpoint_info(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t status) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, MIDI2_CI_ENDPOINT_INFO, version,
                                        src_muid, dst_muid);
  buf[p++] = status;
  return p;
}

/*--------------------------------------------------------------------+
 * Reply to Endpoint Information (0x73) -- Table 11
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_endpoint_info_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t status, const uint8_t *info_data, uint16_t info_len) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, MIDI2_CI_ENDPOINT_INFO_REPLY, version,
                                        src_muid, dst_muid);
  buf[p++] = status;
  midi2_ci_write_14(&buf[p], info_len); p += 2;
  if (info_data && info_len > 0) {
    memcpy(&buf[p], info_data, info_len);
    p += info_len;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * Invalidate MUID (0x7E) -- Table 12
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_invalidate_muid(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t target_muid) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, MIDI2_CI_INVALIDATE_MUID, version,
                                        src_muid, MIDI2_CI_BROADCAST_MUID);
  midi2_ci_write_muid(&buf[p], target_muid); p += 4;
  return p;
}

/*--------------------------------------------------------------------+
 * ACK (0x7D) -- Table 13 (v2+)
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_ack(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id, uint8_t orig_sub_id,
    uint8_t status_code, uint8_t status_data,
    const uint8_t *details, uint16_t msg_len, const uint8_t *msg_text) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_ACK, version,
                                        src_muid, dst_muid);
  buf[p++] = orig_sub_id;
  buf[p++] = status_code;
  buf[p++] = status_data;
  /* 5 bytes details */
  if (details) { memcpy(&buf[p], details, 5); } else { memset(&buf[p], 0, 5); }
  p += 5;
  /* Message text */
  midi2_ci_write_14(&buf[p], msg_len); p += 2;
  if (msg_text && msg_len > 0) {
    memcpy(&buf[p], msg_text, msg_len);
    p += msg_len;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * NAK (0x7F) -- Table 15 (v2+)
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_nak(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id, uint8_t orig_sub_id,
    uint8_t status_code, uint8_t status_data,
    const uint8_t *details, uint16_t msg_len, const uint8_t *msg_text) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_NAK, version,
                                        src_muid, dst_muid);
  if (version >= MIDI2_CI_VERSION_2) {
    buf[p++] = orig_sub_id;
    buf[p++] = status_code;
    buf[p++] = status_data;
    if (details) { memcpy(&buf[p], details, 5); } else { memset(&buf[p], 0, 5); }
    p += 5;
    midi2_ci_write_14(&buf[p], msg_len); p += 2;
    if (msg_text && msg_len > 0) {
      memcpy(&buf[p], msg_text, msg_len);
      p += msg_len;
    }
  }
  return p;
}

/*====================================================================+
 * CATEGORY 2: PROFILE CONFIGURATION MESSAGES
 *====================================================================*/

/*--------------------------------------------------------------------+
 * Profile Inquiry (0x20) -- Table 17
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_inquiry(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id) {
  return midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_INQUIRY, version,
                                  src_muid, dst_muid);
}

/*--------------------------------------------------------------------+
 * Reply to Profile Inquiry (0x21) -- Table 18
 * enabled/disabled: arrays of 5-byte profile IDs
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_inquiry_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id,
    const uint8_t (*enabled)[5], uint16_t enabled_count,
    const uint8_t (*disabled)[5], uint16_t disabled_count) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_INQUIRY_REPLY,
                                        version, src_muid, dst_muid);
  midi2_ci_write_14(&buf[p], enabled_count); p += 2;
  { uint16_t i; for (i = 0; i < enabled_count; i++) { memcpy(&buf[p], enabled[i], 5); p += 5; } }
  midi2_ci_write_14(&buf[p], disabled_count); p += 2;
  { uint16_t i; for (i = 0; i < disabled_count; i++) { memcpy(&buf[p], disabled[i], 5); p += 5; } }
  return p;
}

/*--------------------------------------------------------------------+
 * Set Profile On (0x22) -- Table 24
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_set_profile_on(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id, const uint8_t profile_id[5], uint16_t num_channels) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_SET_PROFILE_ON,
                                        version, src_muid, dst_muid);
  memcpy(&buf[p], profile_id, 5); p += 5;
  if (version >= MIDI2_CI_VERSION_2) {
    midi2_ci_write_14(&buf[p], num_channels); p += 2;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * Set Profile Off (0x23) -- Table 25
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_set_profile_off(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id, const uint8_t profile_id[5]) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_SET_PROFILE_OFF,
                                        version, src_muid, dst_muid);
  memcpy(&buf[p], profile_id, 5); p += 5;
  if (version >= MIDI2_CI_VERSION_2) {
    buf[p++] = 0x00; buf[p++] = 0x00; /* reserved */
  }
  return p;
}

/*--------------------------------------------------------------------+
 * Profile Enabled Report (0x24) -- Table 26
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_enabled(
    uint8_t *buf, uint8_t version, uint32_t src_muid,
    uint8_t device_id, const uint8_t profile_id[5], uint16_t num_channels) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_ENABLED,
                                        version, src_muid, MIDI2_CI_BROADCAST_MUID);
  memcpy(&buf[p], profile_id, 5); p += 5;
  if (version >= MIDI2_CI_VERSION_2) {
    midi2_ci_write_14(&buf[p], num_channels); p += 2;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * Profile Disabled Report (0x25) -- Table 27
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_disabled(
    uint8_t *buf, uint8_t version, uint32_t src_muid,
    uint8_t device_id, const uint8_t profile_id[5], uint16_t num_channels) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_DISABLED,
                                        version, src_muid, MIDI2_CI_BROADCAST_MUID);
  memcpy(&buf[p], profile_id, 5); p += 5;
  if (version >= MIDI2_CI_VERSION_2) {
    midi2_ci_write_14(&buf[p], num_channels); p += 2;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * Profile Added Report (0x26) -- Table 20
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_added(
    uint8_t *buf, uint8_t version, uint32_t src_muid,
    uint8_t device_id, const uint8_t profile_id[5]) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_ADDED,
                                        version, src_muid, MIDI2_CI_BROADCAST_MUID);
  memcpy(&buf[p], profile_id, 5); p += 5;
  return p;
}

/*--------------------------------------------------------------------+
 * Profile Removed Report (0x27) -- Table 21
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_removed(
    uint8_t *buf, uint8_t version, uint32_t src_muid,
    uint8_t device_id, const uint8_t profile_id[5]) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_REMOVED,
                                        version, src_muid, MIDI2_CI_BROADCAST_MUID);
  memcpy(&buf[p], profile_id, 5); p += 5;
  return p;
}

/*--------------------------------------------------------------------+
 * Profile Details Inquiry (0x28) -- Table 22
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_details(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id, const uint8_t profile_id[5], uint8_t inquiry_target) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_DETAILS,
                                        version, src_muid, dst_muid);
  memcpy(&buf[p], profile_id, 5); p += 5;
  buf[p++] = inquiry_target;
  return p;
}

/*--------------------------------------------------------------------+
 * Reply to Profile Details (0x29) -- Table 23
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_details_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id, const uint8_t profile_id[5],
    uint8_t inquiry_target, const uint8_t *data, uint16_t data_len) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_DETAILS_REPLY,
                                        version, src_muid, dst_muid);
  memcpy(&buf[p], profile_id, 5); p += 5;
  buf[p++] = inquiry_target;
  midi2_ci_write_14(&buf[p], data_len); p += 2;
  if (data && data_len > 0) { memcpy(&buf[p], data, data_len); p += data_len; }
  return p;
}

/*--------------------------------------------------------------------+
 * Profile Specific Data (0x2F) -- Table 28
 *
 * Note: When dst_muid is Broadcast, data_len shall not exceed 512 bytes
 * and the message shall use chunking if needed (per spec 7.12).
 * This function does not enforce the limit -- the caller is responsible.
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_profile_specific_data(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id, const uint8_t profile_id[5],
    const uint8_t *data, uint32_t data_len) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PROFILE_SPECIFIC_DATA,
                                        version, src_muid, dst_muid);
  memcpy(&buf[p], profile_id, 5); p += 5;
  midi2_ci_write_28(&buf[p], data_len); p += 4;
  if (data && data_len > 0) {
    memcpy(&buf[p], data, data_len);
    p += (uint16_t)data_len;
  }
  return p;
}

/*====================================================================+
 * CATEGORY 3: PROPERTY EXCHANGE MESSAGES
 *====================================================================*/

/*--------------------------------------------------------------------+
 * PE Capabilities Inquiry (0x30) -- Table 30
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_pe_capability(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t max_simultaneous, uint8_t pe_ver_major, uint8_t pe_ver_minor) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, MIDI2_CI_PE_CAPABILITY, version,
                                        src_muid, dst_muid);
  buf[p++] = max_simultaneous;
  if (version >= MIDI2_CI_VERSION_2) {
    buf[p++] = pe_ver_major;
    buf[p++] = pe_ver_minor;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * Reply to PE Capabilities (0x31) -- Table 32
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_pe_capability_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t max_simultaneous, uint8_t pe_ver_major, uint8_t pe_ver_minor) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, MIDI2_CI_PE_CAPABILITY_REPLY, version,
                                        src_muid, dst_muid);
  buf[p++] = max_simultaneous;
  if (version >= MIDI2_CI_VERSION_2) {
    buf[p++] = pe_ver_major;
    buf[p++] = pe_ver_minor;
  }
  return p;
}

/*--------------------------------------------------------------------+
 * PE Data Message (generic builder for Get/Set/Subscribe/Notify)
 *
 * Used by: 0x34 Get, 0x35 Get Reply, 0x36 Set, 0x37 Set Reply,
 *          0x38 Subscribe, 0x39 Subscribe Reply, 0x3F Notify
 *
 * All PE data messages share the same structure:
 *   header + request_id + header_len + header_data +
 *   num_chunks + this_chunk + body_len + body_data
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_pe_data(
    uint8_t *buf, uint8_t version, uint8_t sub_id,
    uint32_t src_muid, uint32_t dst_muid,
    uint8_t request_id,
    const uint8_t *header_data, uint16_t header_len,
    uint16_t num_chunks, uint16_t this_chunk,
    const uint8_t *body_data, uint16_t body_len) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, sub_id, version,
                                        src_muid, dst_muid);
  buf[p++] = request_id;
  /* Header data */
  midi2_ci_write_14(&buf[p], header_len); p += 2;
  if (header_data && header_len > 0) { memcpy(&buf[p], header_data, header_len); p += header_len; }
  /* Chunk info */
  midi2_ci_write_14(&buf[p], num_chunks); p += 2;
  midi2_ci_write_14(&buf[p], this_chunk); p += 2;
  /* Body data */
  midi2_ci_write_14(&buf[p], body_len); p += 2;
  if (body_data && body_len > 0) { memcpy(&buf[p], body_data, body_len); p += body_len; }
  return p;
}

/* Convenience wrappers */
static inline uint16_t midi2_ci_build_pe_get(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t request_id, const uint8_t *header, uint16_t header_len) {
  return midi2_ci_build_pe_data(buf, version, MIDI2_CI_PE_GET, src_muid, dst_muid,
                                   request_id, header, header_len, 1, 1, NULL, 0);
}

static inline uint16_t midi2_ci_build_pe_get_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t request_id, const uint8_t *header, uint16_t header_len,
    uint16_t num_chunks, uint16_t this_chunk,
    const uint8_t *body, uint16_t body_len) {
  return midi2_ci_build_pe_data(buf, version, MIDI2_CI_PE_GET_REPLY, src_muid, dst_muid,
                                   request_id, header, header_len, num_chunks, this_chunk,
                                   body, body_len);
}

static inline uint16_t midi2_ci_build_pe_set(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t request_id, const uint8_t *header, uint16_t header_len,
    uint16_t num_chunks, uint16_t this_chunk,
    const uint8_t *body, uint16_t body_len) {
  return midi2_ci_build_pe_data(buf, version, MIDI2_CI_PE_SET, src_muid, dst_muid,
                                   request_id, header, header_len, num_chunks, this_chunk,
                                   body, body_len);
}

static inline uint16_t midi2_ci_build_pe_set_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t request_id, const uint8_t *header, uint16_t header_len) {
  return midi2_ci_build_pe_data(buf, version, MIDI2_CI_PE_SET_REPLY, src_muid, dst_muid,
                                   request_id, header, header_len, 1, 1, NULL, 0);
}

static inline uint16_t midi2_ci_build_pe_subscribe(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t request_id, const uint8_t *header, uint16_t header_len,
    uint16_t num_chunks, uint16_t this_chunk,
    const uint8_t *body, uint16_t body_len) {
  return midi2_ci_build_pe_data(buf, version, MIDI2_CI_PE_SUBSCRIBE, src_muid, dst_muid,
                                   request_id, header, header_len, num_chunks, this_chunk,
                                   body, body_len);
}

static inline uint16_t midi2_ci_build_pe_subscribe_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t request_id, const uint8_t *header, uint16_t header_len,
    uint16_t num_chunks, uint16_t this_chunk,
    const uint8_t *body, uint16_t body_len) {
  return midi2_ci_build_pe_data(buf, version, MIDI2_CI_PE_SUBSCRIBE_REPLY, src_muid, dst_muid,
                                   request_id, header, header_len, num_chunks, this_chunk,
                                   body, body_len);
}

static inline uint16_t midi2_ci_build_pe_notify(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t request_id, const uint8_t *header, uint16_t header_len,
    uint16_t num_chunks, uint16_t this_chunk,
    const uint8_t *body, uint16_t body_len) {
  return midi2_ci_build_pe_data(buf, version, MIDI2_CI_PE_NOTIFY, src_muid, dst_muid,
                                   request_id, header, header_len, num_chunks, this_chunk,
                                   body, body_len);
}

/*====================================================================+
 * CATEGORY 4: PROCESS INQUIRY MESSAGES
 *====================================================================*/

/*--------------------------------------------------------------------+
 * PI Capabilities Inquiry (0x40) -- Table 40
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_pi_capability(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid) {
  return midi2_ci_build_header(buf, 0x7F, MIDI2_CI_PI_CAPABILITY, version,
                                  src_muid, dst_muid);
}

/*--------------------------------------------------------------------+
 * Reply to PI Capabilities (0x41) -- Table 41
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_pi_capability_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t supported_features) {
  uint16_t p = midi2_ci_build_header(buf, 0x7F, MIDI2_CI_PI_CAPABILITY_REPLY, version,
                                        src_muid, dst_muid);
  buf[p++] = supported_features;
  return p;
}

/*--------------------------------------------------------------------+
 * MIDI Message Report Inquiry (0x42) -- Table 43
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_pi_midi_report(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id, uint8_t msg_data_control,
    uint8_t system_bitmap, uint8_t reserved,
    uint8_t channel_ctrl_bitmap, uint8_t note_data_bitmap) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PI_MIDI_REPORT, version,
                                        src_muid, dst_muid);
  buf[p++] = msg_data_control;
  buf[p++] = system_bitmap;
  buf[p++] = reserved;
  buf[p++] = channel_ctrl_bitmap;
  buf[p++] = note_data_bitmap;
  return p;
}

/*--------------------------------------------------------------------+
 * Reply to MIDI Message Report (0x43) -- Table 45
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_pi_midi_report_reply(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id,
    uint8_t system_bitmap, uint8_t reserved,
    uint8_t channel_ctrl_bitmap, uint8_t note_data_bitmap) {
  uint16_t p = midi2_ci_build_header(buf, device_id, MIDI2_CI_PI_MIDI_REPORT_REPLY,
                                        version, src_muid, dst_muid);
  buf[p++] = system_bitmap;
  buf[p++] = reserved;
  buf[p++] = channel_ctrl_bitmap;
  buf[p++] = note_data_bitmap;
  return p;
}

/*--------------------------------------------------------------------+
 * End of MIDI Message Report (0x44) -- Table 46
 *--------------------------------------------------------------------*/
static inline uint16_t midi2_ci_build_pi_midi_report_end(
    uint8_t *buf, uint8_t version, uint32_t src_muid, uint32_t dst_muid,
    uint8_t device_id) {
  return midi2_ci_build_header(buf, device_id, MIDI2_CI_PI_MIDI_REPORT_END, version,
                                  src_muid, dst_muid);
}

/* == midi2_dispatch ====================================================== */


/*
 * midi2_dispatch.h - UMP typed dispatch (42 callbacks)
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI 2.0 UMP (M2-104-UM v1.1.2, Nov 2024)
 */




/*--------------------------------------------------------------------+
 * midi2_dispatch -- Typed UMP message dispatch
 *
 * Parses raw UMP words and calls granular, semantically-named callbacks
 * for every message type defined in M2-104-UM v1.1.2.
 *
 * Usage:
 *   midi2_dispatch dp;
 *   midi2_dispatch_init(&dp);
 *   dp.on_note_on = my_note_on_handler;
 *   dp.context = my_app;
 *
 *   // In your receive loop (or as midi2_proc on_ump callback):
 *   midi2_dispatch_feed(&dp, words, word_count);
 *
 * Any callback left NULL is silently skipped (zero overhead beyond
 * the NULL check). The dispatch struct is caller-allocated.
 *--------------------------------------------------------------------*/

/*--------------------------------------------------------------------+
 * MT 0x0: Utility
 *--------------------------------------------------------------------*/
typedef void (*midi2_dp_noop_cb)(void *context);
/* JR Clock and JR Timestamp messages are Groupless per M2-104-UM v1.1.2
 * (section 1.4). The former Group field on the wire is now Reserved; the
 * callbacks accordingly receive only the timestamp. */
typedef void (*midi2_dp_jr_clock_cb)(uint16_t timestamp, void *context);
typedef void (*midi2_dp_jr_timestamp_cb)(uint16_t timestamp, void *context);
typedef void (*midi2_dp_dctpq_cb)(uint16_t tpq, void *context);
typedef void (*midi2_dp_dc_cb)(uint32_t ticks, void *context);

/*--------------------------------------------------------------------+
 * MT 0x1: System Common & System Real Time
 *--------------------------------------------------------------------*/
typedef void (*midi2_dp_system_cb)(uint8_t group, uint8_t status,
                                     uint8_t data1, uint8_t data2, void *context);

/*--------------------------------------------------------------------+
 * MT 0x2: MIDI 1.0 Channel Voice
 *--------------------------------------------------------------------*/
typedef void (*midi2_dp_cv1_note_cb)(uint8_t group, uint8_t channel,
                                       uint8_t note, uint8_t velocity, void *context);
typedef void (*midi2_dp_cv1_cc_cb)(uint8_t group, uint8_t channel,
                                     uint8_t index, uint8_t value, void *context);
typedef void (*midi2_dp_cv1_program_cb)(uint8_t group, uint8_t channel,
                                          uint8_t program, void *context);
typedef void (*midi2_dp_cv1_pressure_cb)(uint8_t group, uint8_t channel,
                                           uint8_t value, void *context);
typedef void (*midi2_dp_cv1_pitch_bend_cb)(uint8_t group, uint8_t channel,
                                             uint16_t value, void *context);
typedef void (*midi2_dp_cv1_poly_pressure_cb)(uint8_t group, uint8_t channel,
                                                uint8_t note, uint8_t value, void *context);

/*--------------------------------------------------------------------+
 * MT 0x4: MIDI 2.0 Channel Voice
 *--------------------------------------------------------------------*/
typedef void (*midi2_dp_note_on_cb)(uint8_t group, uint8_t channel,
                                      uint8_t note, uint16_t velocity,
                                      uint8_t attr_type, uint16_t attr_data, void *context);
typedef void (*midi2_dp_note_off_cb)(uint8_t group, uint8_t channel,
                                       uint8_t note, uint16_t velocity,
                                       uint8_t attr_type, uint16_t attr_data, void *context);
typedef void (*midi2_dp_poly_pressure_cb)(uint8_t group, uint8_t channel,
                                            uint8_t note, uint32_t value, void *context);
typedef void (*midi2_dp_cc_cb)(uint8_t group, uint8_t channel,
                                 uint8_t index, uint32_t value, void *context);
typedef void (*midi2_dp_program_cb)(uint8_t group, uint8_t channel,
                                      uint8_t program, bool bank_valid,
                                      uint8_t bank_msb, uint8_t bank_lsb, void *context);
typedef void (*midi2_dp_chan_pressure_cb)(uint8_t group, uint8_t channel,
                                           uint32_t value, void *context);
typedef void (*midi2_dp_pitch_bend_cb)(uint8_t group, uint8_t channel,
                                         uint32_t value, void *context);
typedef void (*midi2_dp_per_note_pb_cb)(uint8_t group, uint8_t channel,
                                          uint8_t note, uint32_t value, void *context);
typedef void (*midi2_dp_per_note_ctrl_cb)(uint8_t group, uint8_t channel,
                                            uint8_t note, uint8_t index,
                                            uint32_t value, void *context);
typedef void (*midi2_dp_rpn_cb)(uint8_t group, uint8_t channel,
                                  uint8_t bank, uint8_t index,
                                  uint32_t value, void *context);
typedef void (*midi2_dp_per_note_mgmt_cb)(uint8_t group, uint8_t channel,
                                            uint8_t note, bool detach, bool reset,
                                            void *context);

/*--------------------------------------------------------------------+
 * MT 0x3: SysEx7 (raw packet -- reassembly is in midi2_proc)
 * MT 0x5: SysEx8 / Mixed Data Set
 *--------------------------------------------------------------------*/
typedef void (*midi2_dp_sysex7_cb)(uint8_t group, uint8_t status,
                                     const uint8_t *data, uint8_t len, void *context);
typedef void (*midi2_dp_sysex8_cb)(uint8_t group, uint8_t status,
                                     uint8_t stream_id,
                                     const uint8_t *data, uint8_t len, void *context);
typedef void (*midi2_dp_mds_header_cb)(uint8_t group, uint8_t mds_id,
                                         uint16_t num_bytes, uint16_t num_chunks,
                                         uint16_t this_chunk, uint16_t mfr_id,
                                         uint16_t device_id, uint16_t sub_id1,
                                         uint16_t sub_id2, void *context);
/** MDS Payload callback. Always delivers 14 bytes per UMP packet.
 *  The actual valid byte count for the chunk is in the MDS Header's num_bytes field;
 *  the caller must track header state to know when payload ends. */
typedef void (*midi2_dp_mds_payload_cb)(uint8_t group, uint8_t mds_id,
                                          const uint8_t *data, uint8_t len, void *context);

/*--------------------------------------------------------------------+
 * MT 0xD: Flex Data
 *--------------------------------------------------------------------*/
typedef void (*midi2_dp_tempo_cb)(uint8_t group, uint32_t ten_ns_per_qn, void *context);
typedef void (*midi2_dp_time_sig_cb)(uint8_t group, uint8_t numerator,
                                       uint8_t denominator, uint8_t num_32nd_notes,
                                       void *context);
typedef void (*midi2_dp_metronome_cb)(uint8_t group, uint8_t primary_clicks,
                                        uint8_t accent_1, uint8_t accent_2, uint8_t accent_3,
                                        uint8_t subdiv_1, uint8_t subdiv_2, void *context);
typedef void (*midi2_dp_key_sig_cb)(uint8_t group, uint8_t address, uint8_t channel,
                                      int8_t sharps_flats, uint8_t tonic, uint8_t key_type,
                                      void *context);
typedef void (*midi2_dp_chord_cb)(uint8_t group, uint8_t address, uint8_t channel,
                                    int8_t tonic_sf, uint8_t tonic_note, uint8_t chord_type,
                                    uint8_t alt1_type, uint8_t alt1_deg,
                                    uint8_t alt2_type, uint8_t alt2_deg,
                                    uint8_t alt3_type, uint8_t alt3_deg,
                                    uint8_t alt4_type, uint8_t alt4_deg,
                                    int8_t bass_sf, uint8_t bass_note, uint8_t bass_type,
                                    uint8_t bass_alt1_type, uint8_t bass_alt1_deg,
                                    uint8_t bass_alt2_type, uint8_t bass_alt2_deg,
                                    void *context);
typedef void (*midi2_dp_flex_text_cb)(uint8_t group, uint8_t format,
                                        uint8_t address, uint8_t channel,
                                        uint8_t bank, uint8_t status,
                                        const uint8_t *text, uint8_t len, void *context);

/*--------------------------------------------------------------------+
 * MT 0xF: UMP Stream
 *--------------------------------------------------------------------*/
typedef void (*midi2_dp_endpoint_discovery_cb)(uint8_t ump_ver_major, uint8_t ump_ver_minor,
                                                 uint8_t filter, void *context);
typedef void (*midi2_dp_endpoint_info_cb)(uint8_t ump_ver_major, uint8_t ump_ver_minor,
                                            bool static_fb, uint8_t num_fb,
                                            bool midi2_proto, bool midi1_proto,
                                            bool rx_jr, bool tx_jr, void *context);
typedef void (*midi2_dp_device_identity_cb)(uint32_t manufacturer_id,
                                              uint16_t family_id, uint16_t model_id,
                                              uint32_t version_id, void *context);
typedef void (*midi2_dp_stream_text_cb)(uint16_t status, uint8_t format,
                                          const uint8_t *data, uint8_t len, void *context);
typedef void (*midi2_dp_fb_name_cb)(uint8_t format, uint8_t fb_num,
                                      const uint8_t *name, uint8_t len, void *context);
typedef void (*midi2_dp_config_cb)(uint8_t protocol, bool rx_jr, bool tx_jr, void *context);
typedef void (*midi2_dp_fb_discovery_cb)(uint8_t fb_num, uint8_t filter, void *context);
typedef void (*midi2_dp_fb_info_cb)(bool active, uint8_t fb_num,
                                      uint8_t direction, uint8_t ui_hint,
                                      uint8_t first_group, uint8_t num_groups,
                                      uint8_t midi_ci_ver, uint8_t max_sysex8_streams,
                                      uint8_t protocol, void *context);
typedef void (*midi2_dp_clip_cb)(bool start, void *context);

/*--------------------------------------------------------------------+
 * Fallback
 *--------------------------------------------------------------------*/
typedef void (*midi2_dp_unknown_cb)(const uint32_t *words, uint8_t word_count, void *context);

/*--------------------------------------------------------------------+
 * Dispatch State
 *--------------------------------------------------------------------*/
typedef struct {
  void *context;   /**< User pointer passed to all callbacks */

  /** When true, incoming MT 0x2 (MIDI 1.0 CV) messages are translated
   *  to MT 0x4 (MIDI 2.0 CV) with proper value scaling and dispatched
   *  through the on_note_on/on_cc/etc. callbacks. The on_cv1_* callbacks
   *  are NOT called when upscale is active.
   *  When false (default), MT 0x2 goes to on_cv1_* as before. */
  bool upscale_mt2;

  /* MT 0x0: Utility */
  midi2_dp_noop_cb            on_noop;
  midi2_dp_jr_clock_cb        on_jr_clock;
  midi2_dp_jr_timestamp_cb    on_jr_timestamp;
  midi2_dp_dctpq_cb           on_dctpq;
  midi2_dp_dc_cb              on_dc;

  /* MT 0x1: System */
  midi2_dp_system_cb          on_system;

  /* MT 0x2: MIDI 1.0 Channel Voice */
  midi2_dp_cv1_note_cb        on_cv1_note_on;
  midi2_dp_cv1_note_cb        on_cv1_note_off;
  midi2_dp_cv1_poly_pressure_cb on_cv1_poly_pressure;
  midi2_dp_cv1_cc_cb          on_cv1_cc;
  midi2_dp_cv1_program_cb     on_cv1_program;
  midi2_dp_cv1_pressure_cb    on_cv1_chan_pressure;
  midi2_dp_cv1_pitch_bend_cb  on_cv1_pitch_bend;

  /* MT 0x3: SysEx7 (per-packet; use midi2_proc for reassembly) */
  midi2_dp_sysex7_cb          on_sysex7;

  /* MT 0x4: MIDI 2.0 Channel Voice */
  midi2_dp_note_on_cb         on_note_on;
  midi2_dp_note_off_cb        on_note_off;
  midi2_dp_poly_pressure_cb   on_poly_pressure;
  midi2_dp_cc_cb              on_cc;
  midi2_dp_program_cb         on_program;
  midi2_dp_chan_pressure_cb    on_chan_pressure;
  midi2_dp_pitch_bend_cb      on_pitch_bend;
  midi2_dp_per_note_pb_cb     on_per_note_pb;
  midi2_dp_per_note_ctrl_cb   on_reg_per_note;
  midi2_dp_per_note_ctrl_cb   on_asn_per_note;
  midi2_dp_rpn_cb             on_rpn;
  midi2_dp_rpn_cb             on_nrpn;
  midi2_dp_rpn_cb             on_rel_rpn;
  midi2_dp_rpn_cb             on_rel_nrpn;
  midi2_dp_per_note_mgmt_cb   on_per_note_mgmt;

  /* MT 0x5: SysEx8 / Mixed Data Set */
  midi2_dp_sysex8_cb          on_sysex8;
  midi2_dp_mds_header_cb      on_mds_header;
  midi2_dp_mds_payload_cb     on_mds_payload;

  /* MT 0xD: Flex Data */
  midi2_dp_tempo_cb           on_tempo;
  midi2_dp_time_sig_cb        on_time_sig;
  midi2_dp_metronome_cb       on_metronome;
  midi2_dp_key_sig_cb         on_key_sig;
  midi2_dp_chord_cb           on_chord;
  midi2_dp_flex_text_cb       on_flex_text;

  /* MT 0xF: UMP Stream */
  midi2_dp_endpoint_discovery_cb on_endpoint_discovery;
  midi2_dp_endpoint_info_cb      on_endpoint_info;
  midi2_dp_device_identity_cb    on_device_identity;
  midi2_dp_stream_text_cb        on_stream_text;  /* endpoint name, product instance id */
  midi2_dp_fb_name_cb            on_fb_name;      /* function block name (separate: has fb_num) */
  midi2_dp_config_cb             on_config_request;
  midi2_dp_config_cb             on_config_notify;
  midi2_dp_fb_discovery_cb       on_fb_discovery;
  midi2_dp_fb_info_cb            on_fb_info;
  midi2_dp_clip_cb               on_clip;

  /* Fallback for unknown/future MTs */
  midi2_dp_unknown_cb            on_unknown;
} midi2_dispatch;

/*--------------------------------------------------------------------+
 * Functions
 *--------------------------------------------------------------------*/

/** Initialize dispatch, zeroing all callbacks.
 *  Safe to call with NULL dp (function is no-op). */
void midi2_dispatch_init(midi2_dispatch *dp);

/** Feed one UMP message. Parses and dispatches to the appropriate callback.
 *  Precondition: words must contain at least midi2_msg_word_count(mt) valid
 *  words for the message type encoded in words[0]. A shorter word_count causes
 *  a typed message to be dropped rather than read past the buffer; unknown
 *  message types are still forwarded to on_unknown with the given word_count.
 *  Can be used directly as midi2_proc on_ump callback.
 *  Dispatch holds no accumulating state of its own, but the single-context
 *  contract of the midi2_proc/midi2_conv it feeds still applies: do not
 *  re-enter the feeding state's feed from a callback.
 *  Safe to call with NULL context, NULL words, or word_count 0 (function
 *  is no-op). */
void midi2_dispatch_feed(const uint32_t *words, uint8_t word_count, void *context);

/* == midi2_proc ========================================================== */


/*
 * midi2_proc.h - UMP stream processing, group filtering, value scaling
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI 2.0 UMP (M2-104-UM v1.1.2, Nov 2024)
 */




/*--------------------------------------------------------------------+
 * Callback types
 *--------------------------------------------------------------------*/

/* Called for each UMP message that passes the group filter */
typedef void (*midi2_proc_ump_cb)(const uint32_t *words, uint8_t word_count, void *context);

/* Called when a complete SysEx7 message has been reassembled */
typedef void (*midi2_proc_sysex7_cb)(uint8_t group, const uint8_t *data,
                                      uint16_t length, void *context);

/* Called when a complete SysEx8 message has been reassembled */
typedef void (*midi2_proc_sysex8_cb)(uint8_t group, uint8_t stream_id,
                                      const uint8_t *data, uint16_t length, void *context);

/* Called when midi2_proc needs to send UMP words (e.g. SysEx7 fragmented send) */
typedef uint32_t (*midi2_proc_write_fn)(const uint32_t *words, uint32_t count, void *context);

/*--------------------------------------------------------------------+
 * State struct (user-allocated)
 *
 * The caller provides the SysEx reassembly buffer. This allows any
 * buffer size without compile-time limits. Example:
 *
 *   uint8_t sysex_buf[256];
 *   midi2_proc_state proc;
 *   midi2_proc_init(&proc, sysex_buf, sizeof(sysex_buf));
 *--------------------------------------------------------------------*/

typedef struct {
  /** Group filtering: bitmask of which groups to deliver (default 0xFFFF = all) */
  uint16_t group_mask;

  /** Group remap table for outgoing messages (default identity) */
  uint8_t group_map[16];

  /** SysEx7 reassembly: caller-provided buffer */
  uint8_t  *sysex_buf;        /**< pointer to caller's buffer, or NULL to disable */
  uint16_t  sysex_buf_size;   /**< capacity of sysex_buf */
  uint16_t  sysex_len;        /**< current accumulated length */
  uint8_t   sysex_group;      /**< 0xFF = no active SysEx */

  /** SysEx8 reassembly: caller-provided buffer (separate from SysEx7) */
  uint8_t  *sysex8_buf;
  uint16_t  sysex8_buf_size;
  uint16_t  sysex8_len;
  uint8_t   sysex8_group;      /**< 0xFF = no active SysEx8 */
  uint8_t   sysex8_stream_id;  /**< stream ID of active SysEx8 */

  /** Callbacks */
  midi2_proc_ump_cb     on_ump;
  midi2_proc_sysex7_cb  on_sysex7;
  midi2_proc_sysex8_cb  on_sysex8;
  void                 *context;

  /** Debug-only reentrancy guard (see the single-context contract on
   *  midi2_proc_feed). Always present so the struct size matches between debug
   *  and release builds. */
  bool                  in_feed;
} midi2_proc_state;

/*--------------------------------------------------------------------+
 * Functions
 *--------------------------------------------------------------------*/

/** Initialize state with caller-provided SysEx buffers.
 *  @param state           State struct (caller-allocated). Safe to pass NULL
 *                         (function is no-op).
 *  @param sysex7_buf      Buffer for SysEx7 reassembly, or NULL to disable
 *  @param sysex7_buf_size Size of sysex7_buf in bytes
 *  @param sysex8_buf      Buffer for SysEx8 reassembly, or NULL to disable
 *  @param sysex8_buf_size Size of sysex8_buf in bytes */
void midi2_proc_init(midi2_proc_state *state,
                       uint8_t *sysex7_buf, uint16_t sysex7_buf_size,
                       uint8_t *sysex8_buf, uint16_t sysex8_buf_size);

/* Feed UMP words from transport. Processes, filters, dispatches to callbacks.
 * Precondition: words must contain at least midi2_msg_word_count(mt) valid words
 * for the message type encoded in words[0]. A shorter word_count causes the
 * message to be dropped rather than read past the buffer.
 * Safe to call with NULL state or NULL words (function is no-op).
 *
 * Single-context: feed each state instance from one execution context at a
 * time. Do not re-enter feed on the same instance from a callback or another
 * context (e.g. an ISR). Violations are caught by a debug-build assertion
 * (compiled out under NDEBUG). */
void midi2_proc_feed(midi2_proc_state *state, const uint32_t *words, uint8_t word_count);

/* Apply group remap to outgoing words (modifies word 0 in-place).
 * Only remaps message types that have a group field (not Utility or Stream).
 * Safe to call with NULL state or NULL words (function is no-op). */
void midi2_proc_remap_group(midi2_proc_state *state, uint32_t *words);

/* Multi-packet SysEx7 send helper. Fragments data into UMP packets (max 6 bytes each),
 * calls write_fn for each 2-word packet. data does NOT include F0/F7 delimiters.
 * Safe to call with NULL write_fn, NULL data, or length 0 (function is no-op). */
void midi2_proc_send_sysex7(uint8_t group, const uint8_t *data, uint16_t length,
                              midi2_proc_write_fn write_fn, void *context);

/* M2-104-UM §7.1.9 Function Block Name Notification sender.
 * UMP Stream MT 0xF, status 0x12. Fragments the UTF-8 name across
 * Complete / Start / Continue / End 4-word packets (13 name bytes per
 * UMP; total name limited to 91 bytes per spec). Remaining bytes of
 * the final packet are zero-padded per spec.
 * Safe to call with NULL write_fn or NULL name (function is no-op).
 * (v0.2.4+) */
void midi2_proc_send_fb_name(uint8_t fb_idx, const char *name,
                               midi2_proc_write_fn write_fn, void *context);

/* M2-104-UM §7.1.7 Endpoint Name Notification sender.
 * UMP Stream MT 0xF, status 0x003. Fragments UTF-8 name across
 * Complete / Start / Continue / End 4-word packets (14 name bytes
 * per UMP). Empty name sends nothing.
 * Safe to call with NULL write_fn or NULL name (function is no-op).
 * (v0.3.0+) */
void midi2_proc_send_endpoint_name(const char *name,
                                    midi2_proc_write_fn write_fn, void *context);

/* M2-104-UM §7.1.8 Product Instance ID Notification sender.
 * UMP Stream MT 0xF, status 0x004. Fragmentation identical to
 * Endpoint Name (14 bytes per UMP). Empty id sends nothing.
 * Safe to call with NULL write_fn or NULL id (function is no-op).
 * (v0.3.0+) */
void midi2_proc_send_product_id(const char *id,
                                 midi2_proc_write_fn write_fn, void *context);

/* M2-104-UM §7.1.6 Device Identity Notification sender.
 * UMP Stream MT 0xF, status 0x002. Always emits a single 4-word UMP
 * (no fragmentation). Kept for callsite symmetry with the other
 * Stream senders. manufacturer_id uses lower 24 bits only.
 * Safe to call with NULL write_fn (function is no-op).
 * (v0.3.0+) */
void midi2_proc_send_device_identity(uint32_t manufacturer_id,
                                      uint16_t family_id, uint16_t model_id,
                                      uint32_t version_id,
                                      midi2_proc_write_fn write_fn, void *context);

/* M2-104-UM §7.8 SysEx8 sender. MT 0x5. Fragments raw 8-bit data
 * into 4-word packets (13 data bytes per UMP; stream_id rides in
 * word 0 bits [15:8]). status nibble encodes Complete / Start /
 * Continue / End per M2-104-UM Table 14. Zero-length sends nothing.
 * Safe to call with NULL write_fn, NULL data, or length 0 (function
 * is no-op).
 * (v0.3.0+) */
void midi2_proc_send_sysex8(uint8_t group, uint8_t stream_id,
                             const uint8_t *data, uint16_t length,
                             midi2_proc_write_fn write_fn, void *context);

/* == midi2_conv ========================================================== */


/*
 * midi2_conv.h - MIDI 1.0 byte stream to UMP, protocol translation
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI 2.0 UMP (M2-104-UM v1.1.2, Nov 2024)
 */




/*--------------------------------------------------------------------+
 * MIDI 1.0 Byte Stream to UMP Converter
 *
 * Converts serial MIDI 1.0 bytes (DIN-5, TRS, UART) into UMP words.
 * Handles Running Status, multi-byte messages, and SysEx (F0..F7).
 *
 * SysEx is emitted as streaming UMP SysEx7 packets:
 *   - Every 6 bytes: emits START or CONTINUE (2 UMP words)
 *   - On F7: emits END or COMPLETE with remaining bytes
 *   - No caller-provided buffer needed (6-byte internal buffer)
 *
 * Usage:
 *   midi2_conv_state conv;
 *   midi2_conv_init(&conv, 0);  // group 0
 *
 *   // For each incoming byte:
 *   if (midi2_conv_feed(&conv, byte)) {
 *     // conv.ump[] contains the completed UMP message
 *     // conv.ump_words tells how many words (1 or 2)
 *     process(conv.ump, conv.ump_words);
 *   }
 *--------------------------------------------------------------------*/

typedef struct {
  /** Configuration */
  uint8_t group;

  /** Running Status state */
  uint8_t running_status;
  uint8_t data_byte_count;
  uint8_t data_pos;
  uint8_t data[2];

  /** SysEx state: 6-byte internal buffer for streaming */
  uint8_t  sysex_buf[6];       /**< internal buffer (one UMP packet worth) */
  uint8_t  sysex_len;          /**< bytes accumulated in sysex_buf (0-6) */
  bool     in_sysex;           /**< currently inside F0..F7 */
  bool     sysex_started;      /**< true after START emitted */

  /** Output: completed UMP message */
  uint32_t ump[4];
  uint8_t  ump_words;

  /** Debug-only reentrancy guard (see the single-context contract on
   *  midi2_conv_feed). Always present so the struct size matches between debug
   *  and release builds. */
  bool     in_feed;
} midi2_conv_state;

/** Initialize converter state.
 *  @param state         State struct (caller-allocated). Safe to pass NULL
 *                       (function is no-op).
 *  @param group         UMP group to assign to converted messages (0-15). */
void midi2_conv_init(midi2_conv_state *state, uint8_t group);

/* Feed one MIDI 1.0 byte. Returns true when a complete UMP message is ready
 * in state->ump[]. Returns false if more bytes are needed, or if state is
 * NULL (safe to call with NULL state).
 *
 * SysEx of any length is fully supported via streaming UMP SysEx7 packets.
 * Each call produces at most one UMP message (1 or 2 words).
 *
 * Single-context: feed each state instance from one execution context at a
 * time. Do not re-enter feed on the same instance from a callback or another
 * context (e.g. an ISR). Violations are caught by a debug-build assertion
 * (compiled out under NDEBUG). */
bool midi2_conv_feed(midi2_conv_state *state, uint8_t byte);

/* == midi2_ci_dispatch =================================================== */


/*
 * midi2_ci_dispatch.h - MIDI-CI typed dispatch (33 callbacks)
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI-CI (M2-101-UM v1.2, Jun 2023)
 */




/*====================================================================+
 * midi2_ci_dispatch -- Typed MIDI-CI message dispatch
 *
 * Parses reassembled SysEx payloads and calls granular, semantically-
 * named callbacks for every MIDI-CI message type in M2-101-UM v1.2.
 *
 * Usage:
 *   midi2_ci_dispatch dp;
 *   midi2_ci_dispatch_init(&dp);
 *   dp.on_discovery = my_discovery_handler;
 *   dp.context = my_app;
 *
 *   // When a complete CI SysEx arrives (from midi2_proc on_sysex7):
 *   midi2_ci_dispatch_feed(&dp, group, sysex_data, sysex_len);
 *
 * All callbacks receive the common CI header fields (version, src_muid,
 * dst_muid, device_id) plus message-specific fields pre-parsed.
 * NULL callbacks are silently skipped.
 *====================================================================*/

/*--------------------------------------------------------------------+
 * Common header passed to all callbacks
 *--------------------------------------------------------------------*/
typedef struct {
  uint8_t  device_id;
  uint8_t  version;
  uint32_t src_muid;
  uint32_t dst_muid;
  uint8_t  group;      /**< UMP group the SysEx arrived on */
} midi2_ci_header;

/*--------------------------------------------------------------------+
 * Callback types: Management (0x70-0x7F)
 *--------------------------------------------------------------------*/
typedef void (*midi2_ci_dp_discovery_cb)(
    midi2_ci_header hdr, uint32_t mfr_id, uint16_t family, uint16_t model,
    uint32_t sw_rev, uint8_t ci_category, uint32_t max_sysex,
    uint8_t output_path_id, void *context);

typedef void (*midi2_ci_dp_discovery_reply_cb)(
    midi2_ci_header hdr, uint32_t mfr_id, uint16_t family, uint16_t model,
    uint32_t sw_rev, uint8_t ci_category, uint32_t max_sysex,
    uint8_t output_path_id, uint8_t function_block, void *context);

typedef void (*midi2_ci_dp_endpoint_info_cb)(
    midi2_ci_header hdr, uint8_t status, void *context);

typedef void (*midi2_ci_dp_endpoint_info_reply_cb)(
    midi2_ci_header hdr, uint8_t status,
    const uint8_t *info_data, uint16_t info_len, void *context);

typedef void (*midi2_ci_dp_invalidate_muid_cb)(
    midi2_ci_header hdr, uint32_t target_muid, void *context);

typedef void (*midi2_ci_dp_ack_cb)(
    midi2_ci_header hdr, uint8_t orig_sub_id,
    uint8_t status_code, uint8_t status_data,
    const uint8_t *details, uint16_t msg_len, const uint8_t *msg_text,
    void *context);

typedef void (*midi2_ci_dp_nak_cb)(
    midi2_ci_header hdr, uint8_t orig_sub_id,
    uint8_t status_code, uint8_t status_data,
    const uint8_t *details, uint16_t msg_len, const uint8_t *msg_text,
    void *context);

/*--------------------------------------------------------------------+
 * Callback types: Profile Configuration (0x20-0x2F)
 *--------------------------------------------------------------------*/
typedef void (*midi2_ci_dp_profile_inquiry_cb)(
    midi2_ci_header hdr, void *context);

typedef void (*midi2_ci_dp_profile_inquiry_reply_cb)(
    midi2_ci_header hdr,
    uint16_t enabled_count, const uint8_t *enabled_data,
    uint16_t disabled_count, const uint8_t *disabled_data,
    void *context);

typedef void (*midi2_ci_dp_set_profile_cb)(
    midi2_ci_header hdr, const uint8_t *profile_id,
    uint16_t num_channels, void *context);

typedef void (*midi2_ci_dp_profile_report_cb)(
    midi2_ci_header hdr, const uint8_t *profile_id,
    uint16_t num_channels, void *context);

typedef void (*midi2_ci_dp_profile_added_removed_cb)(
    midi2_ci_header hdr, const uint8_t *profile_id, void *context);

typedef void (*midi2_ci_dp_profile_details_cb)(
    midi2_ci_header hdr, const uint8_t *profile_id,
    uint8_t inquiry_target, void *context);

typedef void (*midi2_ci_dp_profile_details_reply_cb)(
    midi2_ci_header hdr, const uint8_t *profile_id,
    uint8_t inquiry_target, const uint8_t *data, uint16_t data_len,
    void *context);

typedef void (*midi2_ci_dp_profile_specific_cb)(
    midi2_ci_header hdr, const uint8_t *profile_id,
    const uint8_t *data, uint32_t data_len, void *context);

/*--------------------------------------------------------------------+
 * Callback types: Property Exchange (0x30-0x3F)
 *--------------------------------------------------------------------*/
typedef void (*midi2_ci_dp_pe_caps_cb)(
    midi2_ci_header hdr, uint8_t max_simultaneous,
    uint8_t pe_ver_major, uint8_t pe_ver_minor, void *context);

typedef void (*midi2_ci_dp_pe_data_cb)(
    midi2_ci_header hdr, uint8_t request_id,
    const uint8_t *header_data, uint16_t header_len,
    uint16_t num_chunks, uint16_t this_chunk,
    const uint8_t *body_data, uint16_t body_len, void *context);

/*--------------------------------------------------------------------+
 * Callback types: Process Inquiry (0x40-0x4F)
 *--------------------------------------------------------------------*/
typedef void (*midi2_ci_dp_pi_caps_cb)(
    midi2_ci_header hdr, void *context);

typedef void (*midi2_ci_dp_pi_caps_reply_cb)(
    midi2_ci_header hdr, uint8_t supported_features, void *context);

typedef void (*midi2_ci_dp_pi_midi_report_cb)(
    midi2_ci_header hdr, uint8_t msg_data_control,
    uint8_t system_bitmap, uint8_t channel_ctrl_bitmap,
    uint8_t note_data_bitmap, void *context);

typedef void (*midi2_ci_dp_pi_midi_report_reply_cb)(
    midi2_ci_header hdr, uint8_t system_bitmap,
    uint8_t channel_ctrl_bitmap, uint8_t note_data_bitmap,
    void *context);

typedef void (*midi2_ci_dp_pi_end_cb)(
    midi2_ci_header hdr, void *context);

/*--------------------------------------------------------------------+
 * Fallback
 *--------------------------------------------------------------------*/
typedef void (*midi2_ci_dp_unknown_cb)(
    midi2_ci_header hdr, uint8_t sub_id,
    const uint8_t *data, uint16_t length, void *context);

/*--------------------------------------------------------------------+
 * Dispatch state
 *--------------------------------------------------------------------*/
typedef struct {
  void *context;

  /* Management */
  midi2_ci_dp_discovery_cb           on_discovery;
  midi2_ci_dp_discovery_reply_cb     on_discovery_reply;
  midi2_ci_dp_endpoint_info_cb       on_endpoint_info;
  midi2_ci_dp_endpoint_info_reply_cb on_endpoint_info_reply;
  midi2_ci_dp_invalidate_muid_cb     on_invalidate_muid;
  midi2_ci_dp_ack_cb                 on_ack;
  midi2_ci_dp_nak_cb                 on_nak;

  /* Profile Configuration */
  midi2_ci_dp_profile_inquiry_cb       on_profile_inquiry;
  midi2_ci_dp_profile_inquiry_reply_cb on_profile_inquiry_reply;
  midi2_ci_dp_set_profile_cb           on_set_profile_on;
  midi2_ci_dp_set_profile_cb           on_set_profile_off;
  midi2_ci_dp_profile_report_cb        on_profile_enabled;
  midi2_ci_dp_profile_report_cb        on_profile_disabled;
  midi2_ci_dp_profile_added_removed_cb on_profile_added;
  midi2_ci_dp_profile_added_removed_cb on_profile_removed;
  midi2_ci_dp_profile_details_cb       on_profile_details;
  midi2_ci_dp_profile_details_reply_cb on_profile_details_reply;
  midi2_ci_dp_profile_specific_cb      on_profile_specific_data;

  /* Property Exchange */
  midi2_ci_dp_pe_caps_cb   on_pe_capability;
  midi2_ci_dp_pe_caps_cb   on_pe_capability_reply;
  midi2_ci_dp_pe_data_cb   on_pe_get;
  midi2_ci_dp_pe_data_cb   on_pe_get_reply;
  midi2_ci_dp_pe_data_cb   on_pe_set;
  midi2_ci_dp_pe_data_cb   on_pe_set_reply;
  midi2_ci_dp_pe_data_cb   on_pe_subscribe;
  midi2_ci_dp_pe_data_cb   on_pe_subscribe_reply;
  midi2_ci_dp_pe_data_cb   on_pe_notify;

  /* Process Inquiry */
  midi2_ci_dp_pi_caps_cb              on_pi_capability;
  midi2_ci_dp_pi_caps_reply_cb        on_pi_capability_reply;
  midi2_ci_dp_pi_midi_report_cb       on_pi_midi_report;
  midi2_ci_dp_pi_midi_report_reply_cb on_pi_midi_report_reply;
  midi2_ci_dp_pi_end_cb               on_pi_midi_report_end;

  /* Fallback */
  midi2_ci_dp_unknown_cb on_unknown;
} midi2_ci_dispatch;

/*--------------------------------------------------------------------+
 * Functions
 *--------------------------------------------------------------------*/

/** Initialize dispatch, zeroing all callbacks.
 *  Safe to call with NULL dp (function is no-op). */
void midi2_ci_dispatch_init(midi2_ci_dispatch *dp);

/** Feed a reassembled SysEx payload (without F0/F7).
 *  Parses the CI header, dispatches to the appropriate callback.
 *  Returns true if the message was recognized as MIDI-CI, false otherwise.
 *  Safe to call with NULL dp or NULL data (returns false).
 *  @param group  UMP group the SysEx arrived on */
bool midi2_ci_dispatch_feed(midi2_ci_dispatch *dp, uint8_t group,
                               const uint8_t *data, uint16_t length);

/* == midi2_ci ============================================================ */


/*
 * midi2_ci.h - MIDI-CI convenience responder
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI-CI (M2-101-UM v1.2, Jun 2023)
 */




/*--------------------------------------------------------------------+
 * Error codes
 *--------------------------------------------------------------------*/
enum {
  MIDI2_CI_OK          =  0,
  MIDI2_CI_ERR_FULL    = -1,   /**< Storage is full (profiles or properties) */
  MIDI2_CI_ERR_NOT_FOUND = -2, /**< Item not found */
  MIDI2_CI_ERR_NULL    = -3,   /**< NULL pointer argument */
};

/* Sub-IDs and constants are now in midi2_ci_msg.h.
 * Legacy alias for backward compatibility: */
#define MIDI2_CI_DISCOVERY_REQUEST  MIDI2_CI_DISCOVERY

/*--------------------------------------------------------------------+
 * Callback types
 *--------------------------------------------------------------------*/

/* Property Exchange getter: returns value string for given property name */
typedef const char* (*midi2_ci_pe_getter)(const char *name, void *context);

/* Property Exchange setter: sets value, returns true on success */
typedef bool (*midi2_ci_pe_setter)(const char *name, const char *value, void *context);

/*--------------------------------------------------------------------+
 * Property descriptor (used in caller-provided array)
 *--------------------------------------------------------------------*/
typedef struct {
  const char         *name;
  const char         *static_value;  /**< used if getter is NULL */
  midi2_ci_pe_getter  getter;
  midi2_ci_pe_setter  setter;
  bool                subscribable;  /**< v0.3.0+: eligible for PE Subscribe */
} midi2_ci_property;

/*--------------------------------------------------------------------+
 * Subscriber registry entry (caller-provided array, v0.3.0+)
 *
 * name_copy holds up to 36 chars per M2-105 PE resource name limit
 * plus NUL terminator, so the responder keeps a stable reference even
 * if the app frees the resource name string passed to subscribe_add.
 *--------------------------------------------------------------------*/
typedef struct {
  uint32_t caller_muid;
  char     name_copy[37];
  uint8_t  in_use;  /**< 0 = free slot; non-zero = active */
} midi2_ci_subscriber;

/*--------------------------------------------------------------------+
 * State struct (user-allocated)
 *
 * The caller provides storage for profiles and properties by pointing
 * to pre-allocated arrays. This allows any capacity without compile-time
 * limits. Example:
 *
 *   uint8_t my_profiles[4][5];
 *   midi2_ci_property my_props[2];
 *   midi2_ci_state ci;
 *   midi2_ci_init(&ci, seed, my_profiles, 4, my_props, 2);
 *--------------------------------------------------------------------*/
/*--------------------------------------------------------------------+
 * RNG callback (v0.2.4+)
 *
 * Platform-specific randomness source. When set via midi2_ci_set_rng(),
 * the convenience responder automatically handles MUID regeneration on
 * Invalidate MUID and peer MUID collisions. Only the lower 28 bits of
 * the returned value are used. Reserved values 0x00000000 and
 * 0x0FFFFFFF (broadcast) are automatically avoided by midi2_ci_new_muid.
 *--------------------------------------------------------------------*/
typedef uint32_t (*midi2_ci_rng_fn)(void *context);

typedef struct {
  /* Device identity (configured by user) */
  uint32_t manufacturer_id;   /**< 3-byte SysEx Manufacturer ID in lower 24 bits */
  uint16_t family_id;
  uint16_t model_id;
  uint32_t version_id;

  /* Capability Inquiry Categories the convenience responder advertises in its
   * Discovery Reply (bitmask of MIDI2_CI_CAT_*). Defaults at init to Profile
   * Config | Property Exchange | Process Inquiry (0x1C). Configure via
   * midi2_ci_set_capabilities. (v0.6.1+) */
  uint8_t ci_cat;

  /* MUID (set at init) */
  uint32_t muid;

  /* Profiles: caller-provided storage */
  uint8_t (*profiles)[5];     /**< pointer to caller's profile array (each 5 bytes) */
  uint8_t  profile_capacity;  /**< max profiles (size of caller's array) */
  uint8_t  profile_count;     /**< current count */

  /* Properties: caller-provided storage */
  midi2_ci_property *properties;  /**< pointer to caller's property array */
  uint8_t  property_capacity;
  uint8_t  property_count;

  /* Write function (how to send SysEx responses back) */
  midi2_proc_write_fn write_fn;
  void               *write_context;

  /* User context for PE callbacks */
  void               *context;

  /* RNG for MUID regeneration (v0.2.4+). NULL = no auto-regen. */
  midi2_ci_rng_fn    rng;
  void              *rng_context;

  /* When true, process_sysex replies with a NAK (Sub-ID#2 0x7F) for any
   * CI sub-id not handled by the convenience responder (v0.2.4+).
   * Default false to preserve v0.2.3 behavior. */
  bool               nak_on_unknown;

  /* When true, the convenience responder broadcasts an Invalidate MUID
   * frame for the old MUID whenever it regenerates because an inbound
   * src_muid collided with ours. M2-101-UM Appendix E 2. Default: true
   * (v0.3.0+). Implementation was already present in v0.2.4 but always
   * on; this flag gates it. */
  bool               auto_invalidate_on_collision;

  /* Subscribers: caller-provided storage (v0.3.0+). NULL when the state
   * was built with the legacy midi2_ci_init (no subscribe/notify). */
  midi2_ci_subscriber *subscribers;
  uint8_t              subscriber_capacity;
  uint8_t              subscriber_count;
} midi2_ci_state;

/*--------------------------------------------------------------------+
 * Functions
 *--------------------------------------------------------------------*/

/** Initialize state with caller-provided storage.
 *  Delegates to midi2_ci_init_ex(..., NULL, 0), so the subscriber
 *  registry is absent and subscribe/notify APIs return ERR_FULL.
 *  @param state         State struct (caller-allocated). Safe to pass NULL
 *                       (function is no-op).
 *  @param muid_seed     Random or unique value for MUID generation (28-bit)
 *  @param profiles      Caller's profile array, or NULL if no profiles needed
 *  @param max_profiles  Capacity of profiles array
 *  @param properties    Caller's property array, or NULL if no properties needed
 *  @param max_properties Capacity of properties array */
void midi2_ci_init(midi2_ci_state *state, uint32_t muid_seed,
                     uint8_t (*profiles)[5], uint8_t max_profiles,
                     midi2_ci_property *properties, uint8_t max_properties);

/** Extended initializer that also wires a subscriber-registry array
 *  for PE Subscribe / Notify. Pass NULL / 0 for the subscribers
 *  argument to match midi2_ci_init semantics. Safe to call with
 *  NULL state (function is no-op).
 *  (v0.3.0+) */
void midi2_ci_init_ex(midi2_ci_state *state, uint32_t muid_seed,
                       uint8_t (*profiles)[5], uint8_t max_profiles,
                       midi2_ci_property *properties, uint8_t max_properties,
                       midi2_ci_subscriber *subscribers, uint8_t max_subscribers);

/** Configure device identity. Safe to call with NULL state (no-op). */
void midi2_ci_set_identity(midi2_ci_state *state,
                             uint32_t manufacturer_id, uint16_t family_id,
                             uint16_t model_id, uint32_t version_id);

/** Configure the Capability Inquiry Categories advertised in the Discovery
 *  Reply (bitmask of MIDI2_CI_CAT_*). Overrides the init default of
 *  Profile Config | Property Exchange | Process Inquiry (0x1C). The declared
 *  MIDI-CI Message Version is derived from the lib's 1.2 support and is not
 *  affected by this call. Safe to call with NULL state (no-op). (v0.6.1+) */
void midi2_ci_set_capabilities(midi2_ci_state *state, uint8_t ci_cat);

/** Set the write function (how CI sends SysEx responses).
 *  Safe to call with NULL state (no-op). */
void midi2_ci_set_write_fn(midi2_ci_state *state,
                              midi2_proc_write_fn write_fn, void *context);

/** Install a platform RNG so the convenience responder can regenerate the
 *  MUID on Invalidate MUID messages and on peer MUID collisions. Without
 *  this, both situations are silently ignored (v0.2.3 behavior).
 *  The callback is invoked from within process_sysex; it must be re-entrant
 *  and should return quickly. Only the lower 28 bits matter.
 *  Safe to call with NULL state (no-op). (v0.2.4+) */
void midi2_ci_set_rng(midi2_ci_state *state,
                         midi2_ci_rng_fn rng, void *context);

/** Enable/disable automatic NAK (Sub-ID#2 0x7F, status 0x01 NOT_SUPPORTED)
 *  replies for CI sub-ids the convenience responder does not handle.
 *  M2-101-UM Appendix E requires a device to "Be able to send a NAK message
 *  when appropriate". Default: false (v0.2.3 compatible).
 *  Safe to call with NULL state (no-op). (v0.2.4+) */
void midi2_ci_set_nak_on_unknown(midi2_ci_state *state, bool enabled);

/** Enable/disable automatic broadcast of an Invalidate MUID frame for the
 *  old MUID whenever the convenience responder regenerates due to an
 *  inbound collision. Default: true.
 *  Safe to call with NULL state (no-op). (v0.3.0+) */
void midi2_ci_set_auto_invalidate_on_collision(midi2_ci_state *state, bool enabled);

/** Generate a fresh 28-bit MUID using the configured RNG, avoiding the
 *  reserved values 0x00000000 and 0x0FFFFFFF (broadcast). If no RNG is
 *  set, falls back to perturbing the current MUID. Returns the new MUID
 *  and also stores it into state->muid. Returns 0u (reserved sentinel)
 *  if state is NULL. (v0.2.4+) */
uint32_t midi2_ci_new_muid(midi2_ci_state *state);

/** Add a profile. Returns MIDI2_CI_OK, MIDI2_CI_ERR_FULL, or
 *  MIDI2_CI_ERR_NULL (state is NULL). */
int midi2_ci_add_profile(midi2_ci_state *state, const uint8_t profile_id[5]);

/** Remove a profile. Returns MIDI2_CI_OK, MIDI2_CI_ERR_NOT_FOUND, or
 *  MIDI2_CI_ERR_NULL (state is NULL). */
int midi2_ci_remove_profile(midi2_ci_state *state, const uint8_t profile_id[5]);

/** Add a static property. Returns MIDI2_CI_OK, MIDI2_CI_ERR_FULL, or
 *  MIDI2_CI_ERR_NULL (state is NULL). */
int midi2_ci_add_property_static(midi2_ci_state *state,
                                    const char *name, const char *value);

/** Add a dynamic property with getter/setter. Returns MIDI2_CI_OK,
 *  MIDI2_CI_ERR_FULL, or MIDI2_CI_ERR_NULL (state is NULL). */
int midi2_ci_add_property_dynamic(midi2_ci_state *state,
                                     const char *name,
                                     midi2_ci_pe_getter getter,
                                     midi2_ci_pe_setter setter);

/** Remove a property by name. Remaining properties are shifted left to
 *  preserve contiguous storage. Returns MIDI2_CI_OK or
 *  MIDI2_CI_ERR_NOT_FOUND. Symmetric with midi2_ci_remove_profile.
 *  Safe to call with NULL state or NULL name (returns
 *  MIDI2_CI_ERR_NULL). (v0.3.0+) */
int midi2_ci_remove_property(midi2_ci_state *state, const char *name);

/** Clear all registered profiles (count-only reset; storage contents are
 *  left intact for caller inspection or reuse).
 *  Safe to call with NULL state (no-op). (v0.3.0+) */
void midi2_ci_reset_profiles(midi2_ci_state *state);

/** Clear all registered properties (count-only reset; storage contents
 *  are left intact for caller inspection or reuse).
 *  Safe to call with NULL state (no-op). (v0.3.0+) */
void midi2_ci_reset_properties(midi2_ci_state *state);

/** Toggle the subscribable flag on a registered property at runtime.
 *  Returns MIDI2_CI_OK or MIDI2_CI_ERR_NOT_FOUND.
 *  Safe to call with NULL state or NULL name (returns
 *  MIDI2_CI_ERR_NULL). (v0.3.0+) */
int midi2_ci_pe_set_subscribable(midi2_ci_state *state,
                                  const char *name, bool subscribable);

/** Register a subscriber (caller_muid) for the named PE resource. The
 *  property must be registered and marked subscribable. Duplicate
 *  (muid, name) pairs are idempotent and return OK.
 *  Safe to call with NULL state or NULL resource_name (returns
 *  MIDI2_CI_ERR_NULL).
 *  @return MIDI2_CI_OK, MIDI2_CI_ERR_NULL (NULL state or resource_name),
 *          MIDI2_CI_ERR_NOT_FOUND (property unknown or not subscribable),
 *          or MIDI2_CI_ERR_FULL (no subscriber capacity, including the case
 *          of midi2_ci_init without a subscribers array). (v0.3.0+) */
int midi2_ci_subscribe_add(midi2_ci_state *state, uint32_t caller_muid,
                            const char *resource_name);

/** Remove a subscriber from the named resource.
 *  Safe to call with NULL state or NULL resource_name (returns
 *  MIDI2_CI_ERR_NULL).
 *  @return MIDI2_CI_OK, MIDI2_CI_ERR_NULL (NULL state or resource_name),
 *          or MIDI2_CI_ERR_NOT_FOUND (subscriber not found). (v0.3.0+) */
int midi2_ci_subscribe_remove(midi2_ci_state *state, uint32_t caller_muid,
                               const char *resource_name);

/** Fan out a PE Notify frame to every subscriber of the named resource.
 *  Returns MIDI2_CI_OK even when the subscriber list is empty, or
 *  MIDI2_CI_ERR_NOT_FOUND when the property is unknown. Emission uses
 *  the state's write_fn (same path as Discovery / PE Reply).
 *  Safe to call with NULL state or NULL resource_name (returns
 *  MIDI2_CI_ERR_NULL). (v0.3.0+) */
int midi2_ci_notify_property_changed(midi2_ci_state *state,
                                      const char *resource_name);

/** Return the current number of active subscribers across all
 *  resources. Returns 0 if state is NULL. (v0.3.0+) */
uint8_t midi2_ci_get_subscriber_count(const midi2_ci_state *state);

/** Process incoming SysEx that might be MIDI-CI.
 *  Returns true if the message was handled (CI), false if not.
 *  Automatically sends Discovery Reply, Profile Inquiry Reply, PE responses.
 *
 *  LIMITATIONS (simplified convenience responder):
 *  - PE Get always returns the first property with a non-NULL value,
 *    regardless of which property was requested (no JSON header parsing).
 *  - PE Set calls the first setter with an empty value string.
 *  - All replies use MIDI-CI Message Version 1 (no v2 extended fields).
 *  - For full PE/Profile control, use midi2_ci_dispatch directly.
 *
 *  @param state   CI state struct. Safe to pass NULL (returns false).
 *  @param group   UMP group the SysEx arrived on (responses go to same group)
 *  @param data    Reassembled SysEx content (no F0/F7)
 *  @param length  Length of data */
bool midi2_ci_process_sysex(midi2_ci_state *state,
                               uint8_t group, const uint8_t *data, uint16_t length);

#ifdef MIDI2_IMPLEMENTATION

/* == midi2_dispatch (impl) =============================================== */


/*
 * midi2_dispatch.c - UMP typed dispatch implementation
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI 2.0 UMP (M2-104-UM v1.1.2, Nov 2024)
 */


/*--------------------------------------------------------------------+
 * Internal helper: sign-extend 4-bit signed nibble to int8_t
 *
 * MIDI 2.0 Flex Data uses 4-bit signed values for "sharps/flats" count
 * in Key Signature and Chord Name messages (range -7..+7, encoded with
 * bit 3 as the sign bit per M2-104-UM §7.5.5 / §7.5.6). Used by
 * dispatch_flex for KEY_SIG and CHORD_NAME (top/bass).
 *--------------------------------------------------------------------*/
static inline int8_t midi2_dispatch_sign_extend_4(uint8_t v) {
  return (v & 0x08u) ? (int8_t)(v | 0xF0u) : (int8_t)(v & 0x0Fu);
}

/*--------------------------------------------------------------------+
 * Init
 *--------------------------------------------------------------------*/
void midi2_dispatch_init(midi2_dispatch *dp) {
  if (dp == NULL) return;
  memset(dp, 0, sizeof(*dp));
}

/*--------------------------------------------------------------------+
 * Internal: dispatch MT 0x0 Utility (1 word)
 *--------------------------------------------------------------------*/
static void dispatch_utility(midi2_dispatch *dp, uint32_t w) {
  /* MT 0x0 is Groupless per M2-104-UM v1.1.2 section 1.4: bits [27:24]
   * are Reserved on every Utility message. The dispatcher does not read
   * them and the JR callbacks do not surface them. */
  uint8_t status = (uint8_t)((w >> 20) & 0x0F);

  switch (status) {
    case MIDI2_UTILITY_NOOP:
      if (dp->on_noop) dp->on_noop(dp->context);
      break;
    case MIDI2_UTILITY_JR_CLOCK:
      if (dp->on_jr_clock) dp->on_jr_clock((uint16_t)(w & 0xFFFF), dp->context);
      break;
    case MIDI2_UTILITY_JR_TIMESTAMP:
      if (dp->on_jr_timestamp) dp->on_jr_timestamp((uint16_t)(w & 0xFFFF), dp->context);
      break;
    case MIDI2_UTILITY_DCTPQ:
      if (dp->on_dctpq) dp->on_dctpq((uint16_t)(w & 0xFFFF), dp->context);
      break;
    case MIDI2_UTILITY_DC:
      if (dp->on_dc) dp->on_dc(w & 0x000FFFFF, dp->context);
      break;
    default:
      if (dp->on_unknown) dp->on_unknown(&w, 1, dp->context);
      break;
  }
}

/*--------------------------------------------------------------------+
 * Internal: dispatch MT 0x1 System (1 word)
 *--------------------------------------------------------------------*/
static void dispatch_system(midi2_dispatch *dp, uint32_t w) {
  if (!dp->on_system) return;
  uint8_t group  = (uint8_t)((w >> 24) & 0x0F);
  uint8_t status = (uint8_t)((w >> 16) & 0xFF);
  uint8_t data1  = (uint8_t)((w >> 8) & 0xFF);
  uint8_t data2  = (uint8_t)(w & 0xFF);
  dp->on_system(group, status, data1, data2, dp->context);
}

/*--------------------------------------------------------------------+
 * Internal: dispatch MT 0x2 MIDI 1.0 Channel Voice (1 word)
 *--------------------------------------------------------------------*/
static void dispatch_cv1(midi2_dispatch *dp, uint32_t w) {
  uint8_t group   = (uint8_t)((w >> 24) & 0x0F);
  uint8_t status  = (uint8_t)((w >> 16) & 0xF0);
  uint8_t channel = (uint8_t)((w >> 16) & 0x0F);
  uint8_t data1   = (uint8_t)((w >> 8) & 0x7F);
  uint8_t data2   = (uint8_t)(w & 0x7F);

  switch (status) {
    case 0x80:
      if (dp->on_cv1_note_off) dp->on_cv1_note_off(group, channel, data1, data2, dp->context);
      break;
    case 0x90:
      if (dp->on_cv1_note_on) dp->on_cv1_note_on(group, channel, data1, data2, dp->context);
      break;
    case 0xA0:
      if (dp->on_cv1_poly_pressure) dp->on_cv1_poly_pressure(group, channel, data1, data2, dp->context);
      break;
    case 0xB0:
      if (dp->on_cv1_cc) dp->on_cv1_cc(group, channel, data1, data2, dp->context);
      break;
    case 0xC0:
      if (dp->on_cv1_program) dp->on_cv1_program(group, channel, data1, dp->context);
      break;
    case 0xD0:
      if (dp->on_cv1_chan_pressure) dp->on_cv1_chan_pressure(group, channel, data1, dp->context);
      break;
    case 0xE0:
      if (dp->on_cv1_pitch_bend) {
        uint16_t pb = (uint16_t)((data2 << 7) | data1);
        dp->on_cv1_pitch_bend(group, channel, pb, dp->context);
      }
      break;
    default:
      if (dp->on_unknown) dp->on_unknown(&w, 1, dp->context);
      break;
  }
}

/*--------------------------------------------------------------------+
 * Internal: dispatch MT 0x3 SysEx7 (2 words)
 *--------------------------------------------------------------------*/
/* reads exactly midi2_msg_word_count(mt) words; the feed-level guard depends on this. */
static void dispatch_sysex7(midi2_dispatch *dp, const uint32_t *w) {
  if (!dp->on_sysex7) return;
  uint8_t group   = (uint8_t)((w[0] >> 24) & 0x0F);
  uint8_t status  = (uint8_t)((w[0] >> 16) & 0xF0); /* matches MIDI2_SYSEX7_* enums */
  uint8_t num     = (uint8_t)((w[0] >> 16) & 0x0F);
  if (num > 6) num = 6;

  uint8_t data[6];
  /* bytes packed: w[0][15:8], w[0][7:0], w[1][31:24]..w[1][7:0] */
  data[0] = (uint8_t)((w[0] >> 8) & 0xFF);
  data[1] = (uint8_t)(w[0] & 0xFF);
  data[2] = (uint8_t)((w[1] >> 24) & 0xFF);
  data[3] = (uint8_t)((w[1] >> 16) & 0xFF);
  data[4] = (uint8_t)((w[1] >> 8) & 0xFF);
  data[5] = (uint8_t)(w[1] & 0xFF);

  dp->on_sysex7(group, status, data, num, dp->context);
}

/*--------------------------------------------------------------------+
 * Internal: dispatch MT 0x4 MIDI 2.0 Channel Voice (2 words)
 *--------------------------------------------------------------------*/
/* reads exactly midi2_msg_word_count(mt) words; the feed-level guard depends on this. */
static void dispatch_cv2(midi2_dispatch *dp, const uint32_t *w) {
  uint8_t group   = (uint8_t)((w[0] >> 24) & 0x0F);
  uint8_t status  = (uint8_t)((w[0] >> 16) & 0xF0);
  uint8_t channel = (uint8_t)((w[0] >> 16) & 0x0F);
  uint8_t byte3   = (uint8_t)((w[0] >> 8) & 0xFF);  /* note/bank/index */
  uint8_t byte4   = (uint8_t)(w[0] & 0xFF);          /* attr_type/index/lsb */

  switch (status) {
    case MIDI2_STATUS_NOTE_ON:
      if (dp->on_note_on) {
        uint16_t velocity = (uint16_t)(w[1] >> 16);
        uint16_t attr_data = (uint16_t)(w[1] & 0xFFFF);
        dp->on_note_on(group, channel, byte3 & 0x7F, velocity, byte4, attr_data, dp->context);
      }
      break;

    case MIDI2_STATUS_NOTE_OFF:
      if (dp->on_note_off) {
        uint16_t velocity = (uint16_t)(w[1] >> 16);
        uint16_t attr_data = (uint16_t)(w[1] & 0xFFFF);
        dp->on_note_off(group, channel, byte3 & 0x7F, velocity, byte4, attr_data, dp->context);
      }
      break;

    case MIDI2_STATUS_POLY_PRESSURE:
      if (dp->on_poly_pressure) dp->on_poly_pressure(group, channel, byte3 & 0x7F, w[1], dp->context);
      break;

    case MIDI2_STATUS_CC:
      if (dp->on_cc) dp->on_cc(group, channel, byte3 & 0x7F, w[1], dp->context);
      break;

    case MIDI2_STATUS_PROGRAM: {
      if (dp->on_program) {
        /* Bank Valid (B) bit lives in byte 4 option flags (LSB) per
         * M2-104-UM section 7.4.9, not in byte 5 MSB. */
        bool bank_valid  = (w[0] & UINT32_C(0x01)) != 0;
        uint8_t program  = (uint8_t)((w[1] >> 24) & 0x7F);
        uint8_t bank_msb = (uint8_t)((w[1] >> 8) & 0x7F);
        uint8_t bank_lsb = (uint8_t)(w[1] & 0x7F);
        dp->on_program(group, channel, program, bank_valid, bank_msb, bank_lsb, dp->context);
      }
      break;
    }

    case MIDI2_STATUS_CHAN_PRESSURE:
      if (dp->on_chan_pressure) dp->on_chan_pressure(group, channel, w[1], dp->context);
      break;

    case MIDI2_STATUS_PITCH_BEND:
      if (dp->on_pitch_bend) dp->on_pitch_bend(group, channel, w[1], dp->context);
      break;

    case MIDI2_STATUS_PER_NOTE_PB:
      if (dp->on_per_note_pb) dp->on_per_note_pb(group, channel, byte3 & 0x7F, w[1], dp->context);
      break;

    case MIDI2_STATUS_REG_PER_NOTE:
      if (dp->on_reg_per_note) dp->on_reg_per_note(group, channel, byte3 & 0x7F, byte4, w[1], dp->context);
      break;

    case MIDI2_STATUS_ASN_PER_NOTE:
      if (dp->on_asn_per_note) dp->on_asn_per_note(group, channel, byte3 & 0x7F, byte4, w[1], dp->context);
      break;

    case MIDI2_STATUS_RPN:
      if (dp->on_rpn) dp->on_rpn(group, channel, byte3 & 0x7F, byte4 & 0x7F, w[1], dp->context);
      break;

    case MIDI2_STATUS_NRPN:
      if (dp->on_nrpn) dp->on_nrpn(group, channel, byte3 & 0x7F, byte4 & 0x7F, w[1], dp->context);
      break;

    case MIDI2_STATUS_REL_RPN:
      if (dp->on_rel_rpn) dp->on_rel_rpn(group, channel, byte3 & 0x7F, byte4 & 0x7F, w[1], dp->context);
      break;

    case MIDI2_STATUS_REL_NRPN:
      if (dp->on_rel_nrpn) dp->on_rel_nrpn(group, channel, byte3 & 0x7F, byte4 & 0x7F, w[1], dp->context);
      break;

    case MIDI2_STATUS_PER_NOTE_MGMT:
      if (dp->on_per_note_mgmt) {
        bool detach = (byte4 & 0x02) != 0;
        bool reset  = (byte4 & 0x01) != 0;
        dp->on_per_note_mgmt(group, channel, byte3 & 0x7F, detach, reset, dp->context);
      }
      break;

    default:
      if (dp->on_unknown) dp->on_unknown(w, 2, dp->context);
      break;
  }
}

/*--------------------------------------------------------------------+
 * Internal: dispatch MT 0x5 Data 128-bit (4 words)
 *--------------------------------------------------------------------*/
/* reads exactly midi2_msg_word_count(mt) words; the feed-level guard depends on this. */
static void dispatch_data128(midi2_dispatch *dp, const uint32_t *w) {
  uint8_t group       = (uint8_t)((w[0] >> 24) & 0x0F);
  uint8_t status_byte = (uint8_t)((w[0] >> 16) & 0xFF);
  uint8_t status_hi   = status_byte & 0xF0;
  uint8_t id_lo       = status_byte & 0x0F;

  if (status_hi == MIDI2_MDS_HEADER) {
    /* Mixed Data Set Header */
    if (dp->on_mds_header) {
      uint16_t num_bytes   = (uint16_t)(w[0] & 0xFFFF);
      uint16_t num_chunks  = (uint16_t)(w[1] >> 16);
      uint16_t this_chunk  = (uint16_t)(w[1] & 0xFFFF);
      uint16_t mfr_id      = (uint16_t)(w[2] >> 16);
      uint16_t device_id   = (uint16_t)(w[2] & 0xFFFF);
      uint16_t sub_id1     = (uint16_t)(w[3] >> 16);
      uint16_t sub_id2     = (uint16_t)(w[3] & 0xFFFF);
      dp->on_mds_header(group, id_lo, num_bytes, num_chunks, this_chunk,
                           mfr_id, device_id, sub_id1, sub_id2, dp->context);
    }
  } else if (status_hi == MIDI2_MDS_PAYLOAD) {
    /* Mixed Data Set Payload */
    if (dp->on_mds_payload) {
      uint8_t data[14];
      uint8_t i;
      for (i = 0; i < 14; i++) {
        uint8_t wi = (uint8_t)((i + 2) / 4);
        uint8_t sh = (uint8_t)(24 - ((i + 2) % 4) * 8);
        data[i] = (uint8_t)((w[wi] >> sh) & 0xFF);
      }
      dp->on_mds_payload(group, id_lo, data, 14, dp->context);
    }
  } else {
    /* SysEx8 (status 0x00..0x30) */
    uint8_t sysex_status = status_byte & 0xF0; /* matches MIDI2_SYSEX8_* enums */
    uint8_t num_bytes    = status_byte & 0x0F;

    if (dp->on_sysex8) {
      uint8_t stream_id = (uint8_t)((w[0] >> 8) & 0xFF);
      /* data bytes: w[0][7:0], w[1][31:0], w[2][31:0], w[3][31:0] = up to 13 */
      uint8_t data[13];
      uint8_t data_len = (num_bytes > 1) ? (uint8_t)(num_bytes - 1) : 0; /* subtract stream_id */
      if (data_len > 13) data_len = 13;
      uint8_t i;
      if (data_len >= 1) data[0] = (uint8_t)(w[0] & 0xFF);
      for (i = 1; i < data_len; i++) {
        uint8_t wi = (uint8_t)(1 + (i - 1) / 4);
        uint8_t sh = (uint8_t)(24 - ((i - 1) % 4) * 8);
        data[i] = (uint8_t)((w[wi] >> sh) & 0xFF);
      }
      dp->on_sysex8(group, sysex_status, stream_id, data, data_len, dp->context);
    } else if (dp->on_unknown) {
      dp->on_unknown(w, 4, dp->context);
    }
  }
}

/*--------------------------------------------------------------------+
 * Internal: dispatch MT 0xD Flex Data (4 words)
 *--------------------------------------------------------------------*/
/* reads exactly midi2_msg_word_count(mt) words; the feed-level guard depends on this. */
static void dispatch_flex(midi2_dispatch *dp, const uint32_t *w) {
  uint8_t group   = (uint8_t)((w[0] >> 24) & 0x0F);
  uint8_t format  = (uint8_t)((w[0] >> 22) & 0x03);
  uint8_t address = (uint8_t)((w[0] >> 20) & 0x03);
  uint8_t channel = (uint8_t)((w[0] >> 16) & 0x0F);
  uint8_t bank    = (uint8_t)((w[0] >> 8) & 0xFF);
  uint8_t status  = (uint8_t)(w[0] & 0xFF);

  if (bank == MIDI2_FLEX_BANK_SETUP) {
    switch (status) {
      case MIDI2_FLEX_TEMPO:
        if (dp->on_tempo) dp->on_tempo(group, w[1], dp->context);
        break;

      case MIDI2_FLEX_TIME_SIG:
        if (dp->on_time_sig) {
          uint8_t num = (uint8_t)((w[1] >> 24) & 0xFF);
          uint8_t den = (uint8_t)((w[1] >> 16) & 0xFF);
          uint8_t n32 = (uint8_t)((w[1] >> 8) & 0xFF);
          dp->on_time_sig(group, num, den, n32, dp->context);
        }
        break;

      case MIDI2_FLEX_METRONOME:
        if (dp->on_metronome) {
          uint8_t clicks = (uint8_t)((w[1] >> 24) & 0xFF);
          uint8_t a1     = (uint8_t)((w[1] >> 16) & 0xFF);
          uint8_t a2     = (uint8_t)((w[1] >> 8) & 0xFF);
          uint8_t a3     = (uint8_t)(w[1] & 0xFF);
          uint8_t s1     = (uint8_t)((w[2] >> 24) & 0xFF);
          uint8_t s2     = (uint8_t)((w[2] >> 16) & 0xFF);
          dp->on_metronome(group, clicks, a1, a2, a3, s1, s2, dp->context);
        }
        break;

      case MIDI2_FLEX_KEY_SIG:
        if (dp->on_key_sig) {
          uint8_t sf_raw  = (uint8_t)((w[1] >> 28) & 0x0F);
          int8_t  sf      = midi2_dispatch_sign_extend_4(sf_raw);
          uint8_t tonic   = (uint8_t)((w[1] >> 24) & 0x0F);
          uint8_t keytype = (uint8_t)((w[1] >> 22) & 0x03);
          dp->on_key_sig(group, address, channel, sf, tonic, keytype, dp->context);
        }
        break;

      case MIDI2_FLEX_CHORD_NAME:
        if (dp->on_chord) {
          uint8_t tsf_raw = (uint8_t)((w[1] >> 28) & 0x0F);
          int8_t  tsf     = midi2_dispatch_sign_extend_4(tsf_raw);
          uint8_t tn  = (uint8_t)((w[1] >> 24) & 0x0F);
          uint8_t ct  = (uint8_t)((w[1] >> 16) & 0xFF);
          uint8_t a1t = (uint8_t)((w[1] >> 12) & 0x0F);
          uint8_t a1d = (uint8_t)((w[1] >> 8) & 0x0F);
          uint8_t a2t = (uint8_t)((w[1] >> 4) & 0x0F);
          uint8_t a2d = (uint8_t)(w[1] & 0x0F);
          uint8_t a3t = (uint8_t)((w[2] >> 28) & 0x0F);
          uint8_t a3d = (uint8_t)((w[2] >> 24) & 0x0F);
          uint8_t a4t = (uint8_t)((w[2] >> 20) & 0x0F);
          uint8_t a4d = (uint8_t)((w[2] >> 16) & 0x0F);
          uint8_t bsf_raw = (uint8_t)((w[3] >> 28) & 0x0F);
          int8_t  bsf     = midi2_dispatch_sign_extend_4(bsf_raw);
          uint8_t bn  = (uint8_t)((w[3] >> 24) & 0x0F);
          uint8_t bt  = (uint8_t)((w[3] >> 16) & 0xFF);
          uint8_t b1t = (uint8_t)((w[3] >> 12) & 0x0F);
          uint8_t b1d = (uint8_t)((w[3] >> 8) & 0x0F);
          uint8_t b2t = (uint8_t)((w[3] >> 4) & 0x0F);
          uint8_t b2d = (uint8_t)(w[3] & 0x0F);
          dp->on_chord(group, address, channel,
                          tsf, tn, ct,
                          a1t, a1d, a2t, a2d, a3t, a3d, a4t, a4d,
                          bsf, bn, bt, b1t, b1d, b2t, b2d,
                          dp->context);
        }
        break;

      default:
        if (dp->on_unknown) dp->on_unknown(w, 4, dp->context);
        break;
    }
  } else if (bank == MIDI2_FLEX_BANK_METADATA || bank == MIDI2_FLEX_BANK_PERF_TEXT) {
    /* Text messages: extract 12 bytes from words 1-3 */
    if (dp->on_flex_text) {
      uint8_t text[12];
      uint8_t i;
      for (i = 0; i < 12; i++) {
        uint8_t wi = (uint8_t)(1 + i / 4);
        uint8_t sh = (uint8_t)(24 - (i % 4) * 8);
        text[i] = (uint8_t)((w[wi] >> sh) & 0xFF);
      }
      /* find actual length (trim trailing zeros in complete/end packets) */
      uint8_t len = 12;
      if (format == 0 || format == 3) {
        while (len > 0 && text[len - 1] == 0) len--;
      }
      dp->on_flex_text(group, format, address, channel, bank, status, text, len, dp->context);
    }
  } else {
    if (dp->on_unknown) dp->on_unknown(w, 4, dp->context);
  }
}

/*--------------------------------------------------------------------+
 * Internal: dispatch MT 0xF UMP Stream (4 words)
 *--------------------------------------------------------------------*/
/* reads exactly midi2_msg_word_count(mt) words; the feed-level guard depends on this. */
static void dispatch_stream(midi2_dispatch *dp, const uint32_t *w) {
  uint8_t  format = (uint8_t)((w[0] >> 26) & 0x03);
  uint16_t status = (uint16_t)((w[0] >> 16) & 0x3FF);

  switch (status) {
    case MIDI2_STREAM_ENDPOINT_DISCOVERY:
      if (dp->on_endpoint_discovery) {
        uint8_t maj = (uint8_t)((w[0] >> 8) & 0xFF);
        uint8_t min = (uint8_t)(w[0] & 0xFF);
        uint8_t filter = (uint8_t)(w[1] & 0xFF);
        dp->on_endpoint_discovery(maj, min, filter, dp->context);
      }
      break;

    case MIDI2_STREAM_ENDPOINT_INFO:
      if (dp->on_endpoint_info) {
        uint8_t maj = (uint8_t)((w[0] >> 8) & 0xFF);
        uint8_t min = (uint8_t)(w[0] & 0xFF);
        bool static_fb = (w[1] & (UINT32_C(1) << 31)) != 0;
        uint8_t num_fb = (uint8_t)((w[1] >> 24) & 0x7F);
        bool m2   = (w[1] & (UINT32_C(1) << 9)) != 0;
        bool m1   = (w[1] & (UINT32_C(1) << 8)) != 0;
        bool rxjr = (w[1] & (UINT32_C(1) << 1)) != 0;
        bool txjr = (w[1] & UINT32_C(1)) != 0;
        dp->on_endpoint_info(maj, min, static_fb, num_fb, m2, m1, rxjr, txjr, dp->context);
      }
      break;

    case MIDI2_STREAM_DEVICE_IDENTITY:
      if (dp->on_device_identity) {
        uint32_t mfr    = (w[1] >> 8) & 0x00FFFFFF;
        uint16_t family = (uint16_t)(w[2] >> 16);
        uint16_t model  = (uint16_t)(w[2] & 0xFFFF);
        uint32_t ver    = w[3];
        dp->on_device_identity(mfr, family, model, ver, dp->context);
      }
      break;

    case MIDI2_STREAM_ENDPOINT_NAME:
    case MIDI2_STREAM_PRODUCT_INSTANCE_ID: {
      if (dp->on_stream_text) {
        /* 2 bytes in w[0][15:0], 12 bytes in w[1..3] = 14 max */
        uint8_t data[14];
        data[0] = (uint8_t)((w[0] >> 8) & 0xFF);
        data[1] = (uint8_t)(w[0] & 0xFF);
        uint8_t i;
        for (i = 0; i < 12; i++) {
          uint8_t wi = (uint8_t)(1 + i / 4);
          uint8_t sh = (uint8_t)(24 - (i % 4) * 8);
          data[2 + i] = (uint8_t)((w[wi] >> sh) & 0xFF);
        }
        uint8_t len = 14;
        if (format == 0 || format == 3) {
          while (len > 0 && data[len - 1] == 0) len--;
        }
        dp->on_stream_text(status, format, data, len, dp->context);
      }
      break;
    }

    case MIDI2_STREAM_FB_NAME: {
      if (dp->on_fb_name) {
        uint8_t fb_num = (uint8_t)((w[0] >> 8) & 0xFF);
        /* 1 byte in w[0][7:0] + 12 in w[1..3] = 13 name bytes max */
        uint8_t name[13];
        name[0] = (uint8_t)(w[0] & 0xFF);
        uint8_t i;
        for (i = 0; i < 12; i++) {
          uint8_t wi = (uint8_t)(1 + i / 4);
          uint8_t sh = (uint8_t)(24 - (i % 4) * 8);
          name[1 + i] = (uint8_t)((w[wi] >> sh) & 0xFF);
        }
        uint8_t len = 13;
        if (format == 0 || format == 3) {
          while (len > 0 && name[len - 1] == 0) len--;
        }
        dp->on_fb_name(format, fb_num, name, len, dp->context);
      }
      break;
    }

    case MIDI2_STREAM_CONFIG_REQUEST:
      if (dp->on_config_request) {
        uint8_t proto = (uint8_t)((w[0] >> 8) & 0xFF);
        bool rxjr = (w[0] & 0x02) != 0;
        bool txjr = (w[0] & 0x01) != 0;
        dp->on_config_request(proto, rxjr, txjr, dp->context);
      }
      break;

    case MIDI2_STREAM_CONFIG_NOTIFY:
      if (dp->on_config_notify) {
        uint8_t proto = (uint8_t)((w[0] >> 8) & 0xFF);
        bool rxjr = (w[0] & 0x02) != 0;
        bool txjr = (w[0] & 0x01) != 0;
        dp->on_config_notify(proto, rxjr, txjr, dp->context);
      }
      break;

    case MIDI2_STREAM_FB_DISCOVERY:
      if (dp->on_fb_discovery) {
        uint8_t fb = (uint8_t)((w[0] >> 8) & 0xFF);
        uint8_t filter = (uint8_t)(w[0] & 0xFF);
        dp->on_fb_discovery(fb, filter, dp->context);
      }
      break;

    case MIDI2_STREAM_FB_INFO:
      if (dp->on_fb_info) {
        bool active     = (w[0] & (UINT32_C(1) << 15)) != 0;
        uint8_t fb_num  = (uint8_t)((w[0] >> 8) & 0x7F);
        uint8_t ui_hint = (uint8_t)((w[0] >> 4) & 0x03);
        uint8_t dir     = (uint8_t)(w[0] & 0x03);
        uint8_t first   = (uint8_t)((w[1] >> 24) & 0x0F);
        uint8_t ngrp    = (uint8_t)((w[1] >> 16) & 0x0F);
        uint8_t ci_ver  = (uint8_t)((w[1] >> 8) & 0xFF);
        uint8_t s8str   = (uint8_t)((w[1] >> 2) & 0x3F);
        uint8_t proto   = (uint8_t)(w[1] & 0x03);
        dp->on_fb_info(active, fb_num, dir, ui_hint, first, ngrp, ci_ver,
                       s8str, proto, dp->context);
      }
      break;

    case MIDI2_STREAM_START_OF_CLIP:
      if (dp->on_clip) dp->on_clip(true, dp->context);
      break;

    case MIDI2_STREAM_END_OF_CLIP:
      if (dp->on_clip) dp->on_clip(false, dp->context);
      break;

    default:
      if (dp->on_unknown) dp->on_unknown(w, 4, dp->context);
      break;
  }
}

/*--------------------------------------------------------------------+
 * Public: feed one UMP message
 *
 * Signature matches midi2_proc_ump_cb so it can be used directly:
 *   proc.on_ump = midi2_dispatch_feed;
 *   proc.context = &dispatch;
 *--------------------------------------------------------------------*/
void midi2_dispatch_feed(const uint32_t *words, uint8_t word_count, void *context) {
  midi2_dispatch *dp = (midi2_dispatch *)context;
  if (dp == NULL || words == NULL || word_count == 0) return;

  uint8_t mt = (uint8_t)((words[0] >> 28) & 0x0F);
  /* Track 1 guard: bail on a typed message whose declared word_count is shorter
   * than its type requires, rather than reading past the buffer. Applied per
   * case so the default/on_unknown path keeps its short-buffer latitude. The
   * guard depends on each multi-word dispatcher reading exactly
   * midi2_msg_word_count(mt) words. */
  uint8_t needed = midi2_msg_word_count(mt);

  switch (mt) {
    case MIDI2_MT_UTILITY:
      dispatch_utility(dp, words[0]);
      break;
    case MIDI2_MT_SYSTEM:
      dispatch_system(dp, words[0]);
      break;
    case MIDI2_MT_MIDI1_CV:
      if (dp->upscale_mt2) {
        uint32_t mt4[2];
        if (midi2_msg_mt2_to_mt4(words[0], mt4)) {
          dispatch_cv2(dp, mt4);
        }
      } else {
        dispatch_cv1(dp, words[0]);
      }
      break;
    case MIDI2_MT_SYSEX7:
      if (word_count < needed) return;
      dispatch_sysex7(dp, words);
      break;
    case MIDI2_MT_MIDI2_CV:
      if (word_count < needed) return;
      dispatch_cv2(dp, words);
      break;
    case MIDI2_MT_DATA128:
      if (word_count < needed) return;
      dispatch_data128(dp, words);
      break;
    case MIDI2_MT_FLEX_DATA:
      if (word_count < needed) return;
      dispatch_flex(dp, words);
      break;
    case MIDI2_MT_STREAM:
      if (word_count < needed) return;
      dispatch_stream(dp, words);
      break;
    default:
      if (dp->on_unknown) dp->on_unknown(words, word_count, dp->context);
      break;
  }
}

/* == midi2_proc (impl) =================================================== */


/*
 * midi2_proc.c - UMP stream processing implementation
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI 2.0 UMP (M2-104-UM v1.1.2, Nov 2024)
 */


/*--------------------------------------------------------------------+
 * Init
 *--------------------------------------------------------------------*/
void midi2_proc_init(midi2_proc_state *state,
                       uint8_t *sysex7_buf, uint16_t sysex7_buf_size,
                       uint8_t *sysex8_buf, uint16_t sysex8_buf_size) {
  uint8_t i;
  if (state == NULL) return;
  memset(state, 0, sizeof(midi2_proc_state));
  state->group_mask = 0xFFFF;
  state->sysex_group = 0xFF;
  state->sysex_buf = sysex7_buf;
  state->sysex_buf_size = sysex7_buf_size;
  state->sysex8_buf = sysex8_buf;
  state->sysex8_buf_size = sysex8_buf_size;
  state->sysex8_group = 0xFF;
  for (i = 0; i < 16; i++) {
    state->group_map[i] = i;
  }
}

/*--------------------------------------------------------------------+
 * SysEx7 reassembly (internal)
 *--------------------------------------------------------------------*/
static void sysex7_process(midi2_proc_state *state, uint8_t group, const uint32_t *words) {
  if (state->sysex_buf == NULL) {
    /* No buffer provided: deliver raw SysEx packets without reassembly */
    return;
  }
  uint8_t status_nib = (words[0] >> 16) & 0xF0;  /* matches MIDI2_SYSEX7_* enums */
  uint8_t num_bytes  = (words[0] >> 16) & 0x0F;

  /* Extract data bytes from SysEx7 UMP packet */
  uint8_t data[6];
  uint8_t n = 0;
  if (num_bytes >= 1) data[n++] = (words[0] >> 8) & 0x7F;
  if (num_bytes >= 2) data[n++] = (words[0] >> 0) & 0x7F;
  if (num_bytes >= 3) data[n++] = (words[1] >> 24) & 0x7F;
  if (num_bytes >= 4) data[n++] = (words[1] >> 16) & 0x7F;
  if (num_bytes >= 5) data[n++] = (words[1] >> 8) & 0x7F;
  if (num_bytes >= 6) data[n++] = (words[1] >> 0) & 0x7F;

  if (status_nib == MIDI2_SYSEX7_COMPLETE) {
    /* Complete: single-packet SysEx */
    if (state->on_sysex7) {
      state->on_sysex7(group, data, n, state->context);
    }
    return;
  }

  if (status_nib == MIDI2_SYSEX7_START) {
    /* Start */
    state->sysex_group = group;
    state->sysex_len = 0;
  } else if (group != state->sysex_group) {
    /* Different group mid-stream: discard in-progress, restart */
    state->sysex_group = group;
    state->sysex_len = 0;
    if (status_nib == MIDI2_SYSEX7_CONTINUE) return;  /* Continue without start: drop */
  }

  /* Append data */
  {
    uint8_t i;
    for (i = 0; i < n && state->sysex_len < state->sysex_buf_size; i++) {
      state->sysex_buf[state->sysex_len++] = data[i];
    }
  }

  if (status_nib == MIDI2_SYSEX7_END) {
    /* End: deliver complete message */
    if (state->on_sysex7) {
      state->on_sysex7(group, state->sysex_buf, state->sysex_len, state->context);
    }
    state->sysex_group = 0xFF;
    state->sysex_len = 0;
  }
}

/*--------------------------------------------------------------------+
 * SysEx8 reassembly (internal)
 *--------------------------------------------------------------------*/
static void sysex8_process(midi2_proc_state *state, uint8_t group, const uint32_t *words) {
  if (state->sysex8_buf == NULL) return;

  uint8_t status_nib = (words[0] >> 16) & 0xF0;  /* matches MIDI2_SYSEX8_* enums */
  uint8_t num_bytes  = (words[0] >> 16) & 0x0F;  /* includes stream_id */
  uint8_t stream_id  = (words[0] >> 8) & 0xFF;

  /* Extract data bytes (num_bytes - 1, since stream_id is counted) */
  uint8_t data[13];
  uint8_t n = 0;
  uint8_t total_data = (num_bytes > 1) ? (uint8_t)(num_bytes - 1) : 0;

  if (total_data >= 1) data[n++] = (uint8_t)(words[0] & 0xFF);
  if (total_data >= 2) data[n++] = (uint8_t)((words[1] >> 24) & 0xFF);
  if (total_data >= 3) data[n++] = (uint8_t)((words[1] >> 16) & 0xFF);
  if (total_data >= 4) data[n++] = (uint8_t)((words[1] >> 8) & 0xFF);
  if (total_data >= 5) data[n++] = (uint8_t)(words[1] & 0xFF);
  if (total_data >= 6) data[n++] = (uint8_t)((words[2] >> 24) & 0xFF);
  if (total_data >= 7) data[n++] = (uint8_t)((words[2] >> 16) & 0xFF);
  if (total_data >= 8) data[n++] = (uint8_t)((words[2] >> 8) & 0xFF);
  if (total_data >= 9) data[n++] = (uint8_t)(words[2] & 0xFF);
  if (total_data >= 10) data[n++] = (uint8_t)((words[3] >> 24) & 0xFF);
  if (total_data >= 11) data[n++] = (uint8_t)((words[3] >> 16) & 0xFF);
  if (total_data >= 12) data[n++] = (uint8_t)((words[3] >> 8) & 0xFF);
  if (total_data >= 13) data[n++] = (uint8_t)(words[3] & 0xFF);

  if (status_nib == MIDI2_SYSEX8_COMPLETE) {
    /* Complete single-packet SysEx8 */
    if (state->on_sysex8) {
      state->on_sysex8(group, stream_id, data, n, state->context);
    }
    return;
  }

  if (status_nib == MIDI2_SYSEX8_START) {
    /* Start */
    state->sysex8_group = group;
    state->sysex8_stream_id = stream_id;
    state->sysex8_len = 0;
  } else if (group != state->sysex8_group || stream_id != state->sysex8_stream_id) {
    /* Different group or stream mid-stream: discard */
    state->sysex8_group = group;
    state->sysex8_stream_id = stream_id;
    state->sysex8_len = 0;
    if (status_nib == MIDI2_SYSEX8_CONTINUE) return;
  }

  /* Append data */
  {
    uint8_t i;
    for (i = 0; i < n && state->sysex8_len < state->sysex8_buf_size; i++) {
      state->sysex8_buf[state->sysex8_len++] = data[i];
    }
  }

  if (status_nib == MIDI2_SYSEX8_END) {
    /* End */
    if (state->on_sysex8) {
      state->on_sysex8(group, state->sysex8_stream_id,
                        state->sysex8_buf, state->sysex8_len, state->context);
    }
    state->sysex8_group = 0xFF;
    state->sysex8_len = 0;
  }
}

/*--------------------------------------------------------------------+
 * Feed
 *--------------------------------------------------------------------*/
/* Inner body; assumes non-NULL state/words. The public wrapper enforces the
 * single-context contract via the in_feed guard around this call. */
static void midi2_proc_feed_inner(midi2_proc_state *state, const uint32_t *words, uint8_t word_count) {
  uint8_t mt = midi2_msg_get_mt(words);
  uint8_t group = midi2_msg_get_group(words);

  /* Track 1 guard: the front message at words[0] must fit in word_count words,
   * else reading the SysEx reassembly payload would over-read the caller's
   * buffer. A truncated message is dropped (a dropped SysEx fragment leaves the
   * in-progress reassembly intact; its bytes are simply missing). */
  if (word_count < midi2_msg_word_count(mt)) return;

  /* SysEx8: reassemble before group filtering (same rationale as SysEx7) */
  if (mt == MIDI2_MT_DATA128) {
    sysex8_process(state, group, words);
  }

  /* SysEx7 is processed before group filtering by design: MIDI-CI responses
   * (delivered via on_sysex7) must work regardless of group filter settings.
   * SysEx7 messages from filtered-out groups will still be reassembled and
   * delivered via on_sysex7. This is intentional: MIDI-CI must work
   * regardless of group filter settings. */
  if (mt == MIDI2_MT_SYSEX7) {
    sysex7_process(state, group, words);
  }

  /* Group filtering: bypass for Utility (MT 0x0) and Stream (MT 0xF) */
  if (mt != MIDI2_MT_UTILITY && mt != MIDI2_MT_STREAM) {
    if (!(state->group_mask & (1u << group))) return;
  }

  /* Dispatch to UMP callback (SysEx7/8 already handled above via callbacks) */
  if (mt != MIDI2_MT_SYSEX7 && mt != MIDI2_MT_DATA128 && state->on_ump) {
    state->on_ump(words, midi2_msg_word_count(mt), state->context);
  }
}

void midi2_proc_feed(midi2_proc_state *state, const uint32_t *words, uint8_t word_count) {
  if (state == NULL || words == NULL) return;
  /* Single-context contract: a callback must not re-enter feed on this state.
   * Caught in debug builds; the set/clear lives only here so no inner return
   * path can leak the flag. */
  MIDI2_ASSERT(!state->in_feed);
  state->in_feed = true;
  midi2_proc_feed_inner(state, words, word_count);
  state->in_feed = false;
}

/*--------------------------------------------------------------------+
 * Group remap
 *--------------------------------------------------------------------*/
void midi2_proc_remap_group(midi2_proc_state *state, uint32_t *words) {
  if (state == NULL || words == NULL) return;
  uint8_t mt = midi2_msg_get_mt(words);
  if (mt != MIDI2_MT_UTILITY && mt != MIDI2_MT_STREAM) {
    uint8_t group = midi2_msg_get_group(words);
    uint8_t new_group = state->group_map[group & 0x0F];
    words[0] = (words[0] & 0xF0FFFFFF) | ((uint32_t)(new_group & 0x0F) << 24);
  }
}

/*--------------------------------------------------------------------+
 * SysEx7 send (fragmentation)
 *--------------------------------------------------------------------*/
void midi2_proc_send_sysex7(uint8_t group, const uint8_t *data, uint16_t length,
                              midi2_proc_write_fn write_fn, void *context) {
  uint32_t w[2];
  uint16_t offset = 0;

  if (write_fn == NULL) return;
  if (data == NULL) return;
  if (length == 0) return;

  if (length <= 6) {
    midi2_msg_sysex7_packet(w, group, MIDI2_SYSEX7_COMPLETE, data, (uint8_t)length);
    write_fn(w, 2, context);
    return;
  }

  /* Start */
  midi2_msg_sysex7_packet(w, group, MIDI2_SYSEX7_START, data, 6);
  write_fn(w, 2, context);
  offset = 6;

  /* Continue */
  while (offset + 6 < length) {
    midi2_msg_sysex7_packet(w, group, MIDI2_SYSEX7_CONTINUE, data + offset, 6);
    write_fn(w, 2, context);
    offset += 6;
  }

  /* End */
  {
    uint8_t remaining = (uint8_t)(length - offset);
    midi2_msg_sysex7_packet(w, group, MIDI2_SYSEX7_END, data + offset, remaining);
    write_fn(w, 2, context);
  }
}

/*--------------------------------------------------------------------+
 * Internal helper: UMP Stream message form code selector
 *
 * Per M2-104-UM §7.1 Table 51, multi-packet stream messages encode
 * their position in the Format field:
 *   Complete = 0 (single-packet message)
 *   Start    = 1 (first of multi-packet)
 *   Continue = 2 (middle of multi-packet)
 *   End      = 3 (final of multi-packet)
 *
 * Used by send_fb_name, stream_text_emit, send_sysex8.
 *--------------------------------------------------------------------*/
static inline uint8_t midi2_proc_stream_form(uint8_t is_first, uint8_t is_last) {
  static const uint8_t lut[4] = {
    [0] = 2u,  /* 00: !first, !last → Continue */
    [1] = 3u,  /* 01: !first,  last → End */
    [2] = 1u,  /* 10:  first, !last → Start */
    [3] = 0u,  /* 11:  first,  last → Complete */
  };
  return lut[((is_first ? 2u : 0u)) | (is_last ? 1u : 0u)];
}

/*--------------------------------------------------------------------+
 * Function Block Name Notification (UMP Stream MT 0xF status 0x12)
 *
 * M2-104-UM §7.1.9. 4-word packet; 1 name byte at byte 3 of word 0 plus
 * 12 more bytes across words 1-3, so 13 bytes of name per UMP. Uses
 * Form = Complete (0) for <=13 bytes, Start/Continue/End otherwise.
 * Spec mandates max 91 bytes total; we silently truncate at 91.
 *--------------------------------------------------------------------*/
#define MIDI2_FB_NAME_BYTES_PER_UMP 13u
#define MIDI2_FB_NAME_MAX_BYTES     91u

void midi2_proc_send_fb_name(uint8_t fb_idx, const char *name,
                               midi2_proc_write_fn write_fn, void *context) {
  if (write_fn == NULL || name == NULL) return;

  uint16_t total = 0;
  while (name[total] && total < MIDI2_FB_NAME_MAX_BYTES) total++;
  if (total == 0) return;

  uint16_t offset = 0;
  while (offset < total) {
    uint16_t remaining = (uint16_t)(total - offset);
    uint8_t  n = (remaining > MIDI2_FB_NAME_BYTES_PER_UMP)
                 ? (uint8_t)MIDI2_FB_NAME_BYTES_PER_UMP
                 : (uint8_t)remaining;
    uint8_t is_first = (offset == 0);
    uint8_t is_last  = (remaining <= MIDI2_FB_NAME_BYTES_PER_UMP);
    uint8_t form = midi2_proc_stream_form(is_first, is_last);

    uint32_t msg[4] = {0};
    msg[0] = ((uint32_t)0xFu << 28)
           | ((uint32_t)form << 26)
           | ((uint32_t)0x12u << 16)
           | ((uint32_t)fb_idx << 8);
    const uint8_t *p = (const uint8_t *)(name + offset);
    if (n > 0) msg[0] |= (uint32_t)p[0];
    uint8_t i;
    for (i = 1; i < n; i++) {
      uint8_t widx  = (uint8_t)(1u + (i - 1u) / 4u);
      uint8_t shift = (uint8_t)(24u - ((i - 1u) % 4u) * 8u);
      msg[widx] |= ((uint32_t)p[i] << shift);
    }
    write_fn(msg, 4, context);
    offset += n;
  }
}

/*--------------------------------------------------------------------+
 * UMP Stream text senders: Endpoint Name (status 0x003),
 * Product Instance ID (status 0x004). M2-104-UM §7.1.7 / §7.1.8.
 *
 * 14 payload bytes per UMP (bytes 0-1 live in word 0 bits [15:0],
 * bytes 2-13 in words 1-3). Fragments into Complete / Start /
 * Continue / End packets. Reuses midi2_msg_stream_endpoint_name /
 * midi2_msg_stream_product_id inline builders so the word layout
 * stays canonical.
 *--------------------------------------------------------------------*/
#define MIDI2_STREAM_TEXT_BYTES_PER_UMP 14u
#define MIDI2_STREAM_TEXT_MAX_BYTES     98u /* 7 UMPs cap per spec */

typedef void (*stream_text_builder_fn)(uint32_t *w, uint8_t format,
                                       const uint8_t *data, uint8_t len);

static void stream_text_emit(stream_text_builder_fn builder,
                              const char *text,
                              midi2_proc_write_fn write_fn, void *context) {
  if (write_fn == NULL || text == NULL) return;
  uint16_t total = 0;
  while (text[total] && total < MIDI2_STREAM_TEXT_MAX_BYTES) total++;
  if (total == 0) return;

  uint16_t offset = 0;
  while (offset < total) {
    uint16_t remaining = (uint16_t)(total - offset);
    uint8_t  n = (remaining > MIDI2_STREAM_TEXT_BYTES_PER_UMP)
                 ? (uint8_t)MIDI2_STREAM_TEXT_BYTES_PER_UMP
                 : (uint8_t)remaining;
    uint8_t is_first = (offset == 0);
    uint8_t is_last  = (remaining <= MIDI2_STREAM_TEXT_BYTES_PER_UMP);
    uint8_t form = midi2_proc_stream_form(is_first, is_last);
    uint32_t msg[4];
    builder(msg, form, (const uint8_t *)(text + offset), n);
    write_fn(msg, 4, context);
    offset += n;
  }
}

void midi2_proc_send_endpoint_name(const char *name,
                                    midi2_proc_write_fn write_fn, void *context) {
  stream_text_emit(midi2_msg_stream_endpoint_name, name, write_fn, context);
}

void midi2_proc_send_product_id(const char *id,
                                 midi2_proc_write_fn write_fn, void *context) {
  stream_text_emit(midi2_msg_stream_product_id, id, write_fn, context);
}

/*--------------------------------------------------------------------+
 * Device Identity Notification sender (M2-104-UM §7.1.6).
 * Single 4-word UMP, no fragmentation. Delegates to the inline
 * builder for byte layout.
 *--------------------------------------------------------------------*/
void midi2_proc_send_device_identity(uint32_t manufacturer_id,
                                      uint16_t family_id, uint16_t model_id,
                                      uint32_t version_id,
                                      midi2_proc_write_fn write_fn, void *context) {
  if (write_fn == NULL) return;
  uint32_t msg[4];
  midi2_msg_stream_device_identity(msg, manufacturer_id, family_id,
                                    model_id, version_id);
  write_fn(msg, 4, context);
}

/*--------------------------------------------------------------------+
 * SysEx8 sender (M2-104-UM §7.8).
 *
 * 13 data bytes per UMP (word 0 low byte carries data[0], words 1-3
 * carry the remaining 12). stream_id rides word 0 bits [15:8]. The
 * status nibble in bits [23:20] encodes Complete/Start/Continue/End
 * per Table 14. Delegates to midi2_msg_sysex8_packet per packet so
 * the status/num_bytes field stays aligned with the canonical
 * builder.
 *--------------------------------------------------------------------*/
#define MIDI2_SYSEX8_BYTES_PER_UMP 13u

void midi2_proc_send_sysex8(uint8_t group, uint8_t stream_id,
                             const uint8_t *data, uint16_t length,
                             midi2_proc_write_fn write_fn, void *context) {
  if (write_fn == NULL) return;
  if (data == NULL) return;
  if (length == 0) return;

  uint16_t offset = 0;
  while (offset < length) {
    uint16_t remaining = (uint16_t)(length - offset);
    uint8_t  n = (remaining > MIDI2_SYSEX8_BYTES_PER_UMP)
                 ? (uint8_t)MIDI2_SYSEX8_BYTES_PER_UMP
                 : (uint8_t)remaining;
    uint8_t is_first = (offset == 0);
    uint8_t is_last  = (remaining <= MIDI2_SYSEX8_BYTES_PER_UMP);
    /* SysEx8 status nibble: form << 4 maps to MIDI2_SYSEX8_* enum values
     * (Complete=0x00, Start=0x10, Continue=0x20, End=0x30). */
    uint8_t status = (uint8_t)(midi2_proc_stream_form(is_first, is_last) << 4);
    uint32_t msg[4];
    midi2_msg_sysex8_packet(msg, group, status, stream_id,
                             data + offset, n);
    write_fn(msg, 4, context);
    offset += n;
  }
}

/* == midi2_conv (impl) =================================================== */


/*
 * midi2_conv.c - MIDI 1.0 byte stream to UMP implementation
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI 2.0 UMP (M2-104-UM v1.1.2, Nov 2024)
 */


void midi2_conv_init(midi2_conv_state *state, uint8_t group) {
  if (state == NULL) return;
  memset(state, 0, sizeof(midi2_conv_state));
  state->group = group & 0x0F;
}

/* How many data bytes a status byte expects */
static uint8_t expected_data_bytes(uint8_t status) {
  switch (status & 0xF0) {
    case 0x80: return 2;  /* Note Off */
    case 0x90: return 2;  /* Note On */
    case 0xA0: return 2;  /* Poly Pressure */
    case 0xB0: return 2;  /* CC */
    case 0xC0: return 1;  /* Program Change */
    case 0xD0: return 1;  /* Channel Pressure */
    case 0xE0: return 2;  /* Pitch Bend */
    default: break;
  }
  /* System Common */
  switch (status) {
    case 0xF1: return 1;  /* MTC Quarter Frame */
    case 0xF2: return 2;  /* Song Position Pointer */
    case 0xF3: return 1;  /* Song Select */
    default:   return 0;  /* System Real-Time, F0, F7, etc */
  }
}

/* Emit a completed channel voice or system common message as UMP */
static void emit_channel_msg(midi2_conv_state *state) {
  uint8_t status = state->running_status;

  if ((status & 0xF0) >= 0x80 && (status & 0xF0) <= 0xE0) {
    /* Channel Voice: MT 0x2 */
    state->ump[0] = midi2_msg_from_midi1(state->group, status,
                                           state->data[0], state->data[1]);
    state->ump_words = 1;
  } else if (status >= 0xF1 && status <= 0xF3) {
    /* System Common: MT 0x1 */
    if (state->data_byte_count == 0) {
      state->ump[0] = midi2_msg_system(state->group, status);
    } else if (state->data_byte_count == 1) {
      state->ump[0] = midi2_msg_system_2byte(state->group, status, state->data[0]);
    } else {
      state->ump[0] = midi2_msg_system_3byte(state->group, status,
                                               state->data[0], state->data[1]);
    }
    state->ump_words = 1;
  }
}

/* Emit a SysEx7 UMP packet from the internal 6-byte buffer.
 * Called when the buffer is full (6 bytes) or when F7 arrives. */
static bool emit_sysex_packet(midi2_conv_state *state, bool is_end) {
  uint8_t status;

  if (!state->sysex_started && is_end) {
    /* Never emitted START: entire SysEx fits in one COMPLETE packet */
    status = MIDI2_SYSEX7_COMPLETE;
  } else if (!state->sysex_started) {
    /* First packet of a multi-packet SysEx */
    status = MIDI2_SYSEX7_START;
    state->sysex_started = true;
  } else if (is_end) {
    /* Final packet */
    status = MIDI2_SYSEX7_END;
  } else {
    /* Middle packet */
    status = MIDI2_SYSEX7_CONTINUE;
  }

  midi2_msg_sysex7_packet(state->ump, state->group, status,
                           state->sysex_buf, state->sysex_len);
  state->ump_words = 2;
  state->sysex_len = 0;

  if (is_end) {
    state->in_sysex = false;
    state->sysex_started = false;
  }

  return true;
}

/* Inner body; assumes non-NULL state. The public wrapper enforces the
 * single-context contract via the in_feed guard around this call. Wrapping
 * (rather than touching each of the many return paths) keeps the set/clear in
 * one place so no path can leak the flag. */
static bool midi2_conv_feed_inner(midi2_conv_state *state, uint8_t byte) {
  state->ump_words = 0;

  /* Real-Time messages (F8-FF) can appear anywhere, even mid-message */
  if (byte >= 0xF8) {
    state->ump[0] = midi2_msg_system(state->group, byte);
    state->ump_words = 1;
    return true;
  }

  /* SysEx handling */
  if (byte == 0xF0) {
    /* SysEx Start */
    state->in_sysex = true;
    state->sysex_started = false;
    state->sysex_len = 0;
    state->running_status = 0;  /* SysEx cancels Running Status */
    return false;
  }

  if (byte == 0xF7) {
    /* SysEx End */
    if (state->in_sysex) {
      return emit_sysex_packet(state, true);
    }
    return false;  /* F7 without F0: ignore */
  }

  if (state->in_sysex) {
    /* A non-Real-Time status byte during SysEx terminates it implicitly */
    if (byte >= 0x80) {
      state->in_sysex = false;
      state->sysex_started = false;
      state->sysex_len = 0;
      /* Fall through to process as new status byte */
    } else {
      /* Accumulate SysEx data byte */
      state->sysex_buf[state->sysex_len++] = byte;
      if (state->sysex_len == 6) {
        /* Buffer full: emit START or CONTINUE packet */
        return emit_sysex_packet(state, false);
      }
      return false;
    }
  }

  /* Status byte */
  if (byte >= 0x80) {
    /* System Common (F1-F6) cancel Running Status */
    if (byte >= 0xF1 && byte <= 0xF6) {
      state->running_status = byte;
      state->data_byte_count = expected_data_bytes(byte);
      state->data_pos = 0;
      if (state->data_byte_count == 0) {
        /* Tune Request (F6) -- no data bytes */
        state->ump[0] = midi2_msg_system(state->group, byte);
        state->ump_words = 1;
        state->running_status = 0;
        return true;
      }
      return false;
    }

    /* Channel Voice status */
    state->running_status = byte;
    state->data_byte_count = expected_data_bytes(byte);
    state->data_pos = 0;
    state->data[0] = 0;
    state->data[1] = 0;
    return false;
  }

  /* Data byte (Running Status applies) */
  if (state->running_status == 0) {
    return false;  /* No status set: orphan data byte, ignore */
  }

  state->data[state->data_pos++] = byte;

  if (state->data_pos >= state->data_byte_count) {
    emit_channel_msg(state);
    state->data_pos = 0;  /* Reset for Running Status (next data bytes reuse status) */
    state->data[0] = 0;
    state->data[1] = 0;
    return true;
  }

  return false;
}

bool midi2_conv_feed(midi2_conv_state *state, uint8_t byte) {
  bool r;
  if (state == NULL) return false;
  /* Single-context contract: a callback must not re-enter feed on this state.
   * Caught in debug builds; the set/clear lives only here so no inner return
   * path can leak the flag. */
  MIDI2_ASSERT(!state->in_feed);
  state->in_feed = true;
  r = midi2_conv_feed_inner(state, byte);
  state->in_feed = false;
  return r;
}

/* == midi2_ci_dispatch (impl) ============================================ */


/*
 * midi2_ci_dispatch.c - MIDI-CI typed dispatch implementation
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI-CI (M2-101-UM v1.2, Jun 2023)
 */


void midi2_ci_dispatch_init(midi2_ci_dispatch *dp) {
  if (dp == NULL) return;
  memset(dp, 0, sizeof(*dp));
}

/*--------------------------------------------------------------------+
 * Internal: build common header struct from raw data
 *--------------------------------------------------------------------*/
static midi2_ci_header make_hdr(const uint8_t *d, uint8_t group) {
  midi2_ci_header h;
  h.device_id = midi2_ci_get_device_id(d);
  h.version   = midi2_ci_get_version(d);
  h.src_muid  = midi2_ci_get_src_muid(d);
  h.dst_muid  = midi2_ci_get_dst_muid(d);
  h.group     = group;
  return h;
}

/*--------------------------------------------------------------------+
 * Internal: parse PE data message (shared by Get/Set/Subscribe/Notify)
 *--------------------------------------------------------------------*/
static void dispatch_pe_data(midi2_ci_dp_pe_data_cb cb, midi2_ci_header hdr,
                                const uint8_t *d, uint16_t len, void *ctx) {
  if (cb == NULL) return;
  if (len < 14) return;  /* header(13) + request_id(1) minimum */
  uint8_t request_id = d[13];
  uint16_t p = 14;

  /* Header data */
  uint16_t hdr_len = 0;
  if (p + 2 <= len) { hdr_len = midi2_ci_read_14(&d[p]); p += 2; }
  const uint8_t *hdr_data = (hdr_len > 0 && p + hdr_len <= len) ? &d[p] : NULL;
  if (hdr_len > 0) p += hdr_len;

  /* Chunk info */
  uint16_t num_chunks = 0, this_chunk = 0;
  if (p + 2 <= len) { num_chunks = midi2_ci_read_14(&d[p]); p += 2; }
  if (p + 2 <= len) { this_chunk = midi2_ci_read_14(&d[p]); p += 2; }

  /* Body data */
  uint16_t body_len = 0;
  if (p + 2 <= len) { body_len = midi2_ci_read_14(&d[p]); p += 2; }
  const uint8_t *body_data = (body_len > 0 && p + body_len <= len) ? &d[p] : NULL;

  cb(hdr, request_id, hdr_data, hdr_len, num_chunks, this_chunk,
        body_data, body_len, ctx);
}

/*--------------------------------------------------------------------+
 * Feed
 *--------------------------------------------------------------------*/
bool midi2_ci_dispatch_feed(midi2_ci_dispatch *dp, uint8_t group,
                               const uint8_t *data, uint16_t length) {
  if (dp == NULL || data == NULL) return false;
  if (!midi2_ci_is_ci(data, length)) return false;

  midi2_ci_header hdr = make_hdr(data, group);
  uint8_t sub_id = midi2_ci_get_sub_id(data);

  switch (sub_id) {

    /*--- Management ---*/

    case MIDI2_CI_DISCOVERY: {
      if (dp->on_discovery == NULL || length < 29) break;
      uint8_t out_path = (hdr.version >= MIDI2_CI_VERSION_2 && length >= 30) ? data[29] : 0;
      dp->on_discovery(hdr, midi2_ci_get_mfr_id(data), midi2_ci_get_family(data),
                           midi2_ci_get_model(data), midi2_ci_get_sw_rev(data),
                           midi2_ci_get_ci_category(data), midi2_ci_get_max_sysex(data),
                           out_path, dp->context);
      return true;
    }

    case MIDI2_CI_DISCOVERY_REPLY: {
      if (dp->on_discovery_reply == NULL || length < 29) break;
      uint8_t out_path = 0, fb = 0x7F;
      if (hdr.version >= MIDI2_CI_VERSION_2 && length >= 31) {
        out_path = data[29];
        fb = data[30];
      }
      dp->on_discovery_reply(hdr, midi2_ci_get_mfr_id(data), midi2_ci_get_family(data),
                                 midi2_ci_get_model(data), midi2_ci_get_sw_rev(data),
                                 midi2_ci_get_ci_category(data), midi2_ci_get_max_sysex(data),
                                 out_path, fb, dp->context);
      return true;
    }

    case MIDI2_CI_ENDPOINT_INFO: {
      if (dp->on_endpoint_info == NULL || length < 14) break;
      dp->on_endpoint_info(hdr, data[13], dp->context);
      return true;
    }

    case MIDI2_CI_ENDPOINT_INFO_REPLY: {
      if (dp->on_endpoint_info_reply == NULL || length < 16) break;
      uint8_t status = data[13];
      uint16_t info_len = midi2_ci_read_14(&data[14]);
      const uint8_t *info = (info_len > 0 && length >= 16 + info_len) ? &data[16] : NULL;
      dp->on_endpoint_info_reply(hdr, status, info, info_len, dp->context);
      return true;
    }

    case MIDI2_CI_INVALIDATE_MUID: {
      if (dp->on_invalidate_muid == NULL || length < 17) break;
      dp->on_invalidate_muid(hdr, midi2_ci_get_target_muid(data), dp->context);
      return true;
    }

    case MIDI2_CI_ACK: {
      if (dp->on_ack == NULL || length < 13) break;
      uint8_t orig = 0, sc = 0, sd = 0;
      const uint8_t *det = NULL;
      uint16_t ml = 0;
      const uint8_t *mt = NULL;
      if (length >= 23) {
        orig = data[13]; sc = data[14]; sd = data[15];
        det = &data[16]; /* 5 bytes */
        ml = midi2_ci_read_14(&data[21]);
        mt = (ml > 0 && length >= 23 + ml) ? &data[23] : NULL;
      }
      dp->on_ack(hdr, orig, sc, sd, det, ml, mt, dp->context);
      return true;
    }

    case MIDI2_CI_NAK: {
      if (dp->on_nak == NULL || length < 13) break;
      uint8_t orig = 0, sc = 0, sd = 0;
      const uint8_t *det = NULL;
      uint16_t ml = 0;
      const uint8_t *mt = NULL;
      if (hdr.version >= MIDI2_CI_VERSION_2 && length >= 23) {
        orig = data[13]; sc = data[14]; sd = data[15];
        det = &data[16];
        ml = midi2_ci_read_14(&data[21]);
        mt = (ml > 0 && length >= 23 + ml) ? &data[23] : NULL;
      }
      dp->on_nak(hdr, orig, sc, sd, det, ml, mt, dp->context);
      return true;
    }

    /*--- Profile Configuration ---*/

    case MIDI2_CI_PROFILE_INQUIRY: {
      if (dp->on_profile_inquiry == NULL) break;
      dp->on_profile_inquiry(hdr, dp->context);
      return true;
    }

    case MIDI2_CI_PROFILE_INQUIRY_REPLY: {
      if (dp->on_profile_inquiry_reply == NULL || length < 15) break;
      uint16_t en_count = midi2_ci_read_14(&data[13]);
      uint16_t en_bytes = (uint16_t)(en_count * 5);
      if (15 + en_bytes + 2 > length) break;  /* bounds check */
      const uint8_t *en_data = &data[15];
      uint16_t dis_off = (uint16_t)(15 + en_bytes);
      uint16_t dis_count = 0;
      const uint8_t *dis_data = NULL;
      if (dis_off + 2 <= length) {
        dis_count = midi2_ci_read_14(&data[dis_off]);
        dis_data = &data[dis_off + 2];
      }
      dp->on_profile_inquiry_reply(hdr, en_count, en_data, dis_count, dis_data, dp->context);
      return true;
    }

    case MIDI2_CI_SET_PROFILE_ON: {
      if (dp->on_set_profile_on == NULL || length < 18) break;
      uint16_t nch = 0;
      if (hdr.version >= MIDI2_CI_VERSION_2 && length >= 20) {
        nch = midi2_ci_read_14(&data[18]);
      }
      dp->on_set_profile_on(hdr, &data[13], nch, dp->context);
      return true;
    }

    case MIDI2_CI_SET_PROFILE_OFF: {
      if (dp->on_set_profile_off == NULL || length < 18) break;
      dp->on_set_profile_off(hdr, &data[13], 0, dp->context);
      return true;
    }

    case MIDI2_CI_PROFILE_ENABLED: {
      if (dp->on_profile_enabled == NULL || length < 18) break;
      uint16_t nch = 0;
      if (hdr.version >= MIDI2_CI_VERSION_2 && length >= 20) {
        nch = midi2_ci_read_14(&data[18]);
      }
      dp->on_profile_enabled(hdr, &data[13], nch, dp->context);
      return true;
    }

    case MIDI2_CI_PROFILE_DISABLED: {
      if (dp->on_profile_disabled == NULL || length < 18) break;
      uint16_t nch = 0;
      if (hdr.version >= MIDI2_CI_VERSION_2 && length >= 20) {
        nch = midi2_ci_read_14(&data[18]);
      }
      dp->on_profile_disabled(hdr, &data[13], nch, dp->context);
      return true;
    }

    case MIDI2_CI_PROFILE_ADDED: {
      if (dp->on_profile_added == NULL || length < 18) break;
      dp->on_profile_added(hdr, &data[13], dp->context);
      return true;
    }

    case MIDI2_CI_PROFILE_REMOVED: {
      if (dp->on_profile_removed == NULL || length < 18) break;
      dp->on_profile_removed(hdr, &data[13], dp->context);
      return true;
    }

    case MIDI2_CI_PROFILE_DETAILS: {
      if (dp->on_profile_details == NULL || length < 19) break;
      dp->on_profile_details(hdr, &data[13], data[18], dp->context);
      return true;
    }

    case MIDI2_CI_PROFILE_DETAILS_REPLY: {
      if (dp->on_profile_details_reply == NULL || length < 21) break;
      uint8_t target = data[18];
      uint16_t dl = midi2_ci_read_14(&data[19]);
      const uint8_t *dd = (dl > 0 && length >= 21 + dl) ? &data[21] : NULL;
      dp->on_profile_details_reply(hdr, &data[13], target, dd, dl, dp->context);
      return true;
    }

    case MIDI2_CI_PROFILE_SPECIFIC_DATA: {
      if (dp->on_profile_specific_data == NULL || length < 22) break;
      uint32_t dl = midi2_ci_read_28(&data[18]);
      const uint8_t *dd = (dl > 0 && dl <= (uint32_t)(length - 22)) ? &data[22] : NULL;
      dp->on_profile_specific_data(hdr, &data[13], dd, dl, dp->context);
      return true;
    }

    /*--- Property Exchange ---*/

    case MIDI2_CI_PE_CAPABILITY: {
      if (dp->on_pe_capability == NULL || length < 14) break;
      uint8_t max_sim = data[13];
      uint8_t maj = 0, min = 0;
      if (hdr.version >= MIDI2_CI_VERSION_2 && length >= 16) {
        maj = data[14]; min = data[15];
      }
      dp->on_pe_capability(hdr, max_sim, maj, min, dp->context);
      return true;
    }

    case MIDI2_CI_PE_CAPABILITY_REPLY: {
      if (dp->on_pe_capability_reply == NULL || length < 14) break;
      uint8_t max_sim = data[13];
      uint8_t maj = 0, min = 0;
      if (hdr.version >= MIDI2_CI_VERSION_2 && length >= 16) {
        maj = data[14]; min = data[15];
      }
      dp->on_pe_capability_reply(hdr, max_sim, maj, min, dp->context);
      return true;
    }

    case MIDI2_CI_PE_GET:
      dispatch_pe_data(dp->on_pe_get, hdr, data, length, dp->context);
      return dp->on_pe_get != NULL;

    case MIDI2_CI_PE_GET_REPLY:
      dispatch_pe_data(dp->on_pe_get_reply, hdr, data, length, dp->context);
      return dp->on_pe_get_reply != NULL;

    case MIDI2_CI_PE_SET:
      dispatch_pe_data(dp->on_pe_set, hdr, data, length, dp->context);
      return dp->on_pe_set != NULL;

    case MIDI2_CI_PE_SET_REPLY:
      dispatch_pe_data(dp->on_pe_set_reply, hdr, data, length, dp->context);
      return dp->on_pe_set_reply != NULL;

    case MIDI2_CI_PE_SUBSCRIBE:
      dispatch_pe_data(dp->on_pe_subscribe, hdr, data, length, dp->context);
      return dp->on_pe_subscribe != NULL;

    case MIDI2_CI_PE_SUBSCRIBE_REPLY:
      dispatch_pe_data(dp->on_pe_subscribe_reply, hdr, data, length, dp->context);
      return dp->on_pe_subscribe_reply != NULL;

    case MIDI2_CI_PE_NOTIFY:
      dispatch_pe_data(dp->on_pe_notify, hdr, data, length, dp->context);
      return dp->on_pe_notify != NULL;

    /*--- Process Inquiry ---*/

    case MIDI2_CI_PI_CAPABILITY: {
      if (dp->on_pi_capability == NULL) break;
      dp->on_pi_capability(hdr, dp->context);
      return true;
    }

    case MIDI2_CI_PI_CAPABILITY_REPLY: {
      if (dp->on_pi_capability_reply == NULL || length < 14) break;
      dp->on_pi_capability_reply(hdr, data[13], dp->context);
      return true;
    }

    case MIDI2_CI_PI_MIDI_REPORT: {
      if (dp->on_pi_midi_report == NULL || length < 18) break;
      dp->on_pi_midi_report(hdr, data[13], data[14],
                                data[16], data[17], dp->context);
      return true;
    }

    case MIDI2_CI_PI_MIDI_REPORT_REPLY: {
      if (dp->on_pi_midi_report_reply == NULL || length < 17) break;
      dp->on_pi_midi_report_reply(hdr, data[13], data[15], data[16], dp->context);
      return true;
    }

    case MIDI2_CI_PI_MIDI_REPORT_END: {
      if (dp->on_pi_midi_report_end == NULL) break;
      dp->on_pi_midi_report_end(hdr, dp->context);
      return true;
    }

    default:
      break;
  }

  /* Unknown or no callback registered */
  if (dp->on_unknown) {
    dp->on_unknown(hdr, sub_id, data, length, dp->context);
  }
  return false;
}

/* == midi2_ci (impl) ===================================================== */


/*
 * midi2_ci.c - MIDI-CI convenience responder implementation
 *
 * Part of midi2 - Portable MIDI 2.0 library (C99)
 * https://github.com/sauloverissimo/midi2
 *
 * Spec: MIDI-CI (M2-101-UM v1.2, Jun 2023)
 */


/* Message Version declared on every reply this convenience Responder emits.
 *
 * The responder implements MIDI-CI 1.2 in full: it advertises Process Inquiry
 * and Property Exchange (both 1.2 categories) and its replies carry the v2
 * message fields (Discovery Reply Output Path Id + Function Block, PE
 * Capability major/minor, NAK details). It therefore declares version 0x02
 * uniformly. Keeping a single constant guarantees two invariants:
 *   1. Advertising a 1.2 category (Process Inquiry 0x10) while claiming
 *      version 0x01 is unrepresentable (the bug the Workbench flagged with
 *      "Process Inquiry not allowed on Message Format Version 0x01 Devices").
 *   2. All replies share one version, so an Initiator never sees
 *      "MIDI-CI Message Format Version has Changed" between messages.
 * ci_cat stays configurable (midi2_ci_set_capabilities); the declared version
 * does not, because the lib always speaks the 1.2 message format. */
#define CI_RESPONDER_VERSION  MIDI2_CI_VERSION_2

/*--------------------------------------------------------------------+
 * Init
 *--------------------------------------------------------------------*/
void midi2_ci_init_ex(midi2_ci_state *state, uint32_t muid_seed,
                       uint8_t (*profiles)[5], uint8_t max_profiles,
                       midi2_ci_property *properties, uint8_t max_properties,
                       midi2_ci_subscriber *subscribers, uint8_t max_subscribers) {
  if (state == NULL) return;
  memset(state, 0, sizeof(midi2_ci_state));
  state->muid = muid_seed & 0x0FFFFFFF;
  state->profiles = profiles;
  state->profile_capacity = max_profiles;
  state->properties = properties;
  state->property_capacity = max_properties;
  state->subscribers = subscribers;
  state->subscriber_capacity = max_subscribers;
  /* Clear caller-provided subscriber slots so the `in_use` sentinel starts
   * zero. That field is a midi2 implementation detail, not part of the
   * caller contract, so init owns it. */
  if (subscribers != NULL && max_subscribers > 0) {
    memset(subscribers, 0, sizeof(midi2_ci_subscriber) * max_subscribers);
  }
  state->auto_invalidate_on_collision = true; /* v0.3.0+ default on */
  /* Advertise every CI Category the convenience responder actually handles:
   * Profile Config, Property Exchange, Process Inquiry (0x1C). (v0.6.1+) */
  state->ci_cat = MIDI2_CI_CAT_PROFILE_CONFIG
                | MIDI2_CI_CAT_PROPERTY_EXCHANGE
                | MIDI2_CI_CAT_PROCESS_INQUIRY;
}

void midi2_ci_init(midi2_ci_state *state, uint32_t muid_seed,
                     uint8_t (*profiles)[5], uint8_t max_profiles,
                     midi2_ci_property *properties, uint8_t max_properties) {
  midi2_ci_init_ex(state, muid_seed,
                    profiles, max_profiles,
                    properties, max_properties,
                    NULL, 0);
}

void midi2_ci_set_identity(midi2_ci_state *state,
                             uint32_t manufacturer_id, uint16_t family_id,
                             uint16_t model_id, uint32_t version_id) {
  if (state == NULL) return;
  state->manufacturer_id = manufacturer_id;
  state->family_id = family_id;
  state->model_id = model_id;
  state->version_id = version_id;
}

void midi2_ci_set_capabilities(midi2_ci_state *state, uint8_t ci_cat) {
  if (state == NULL) return;
  state->ci_cat = ci_cat;
}

void midi2_ci_set_write_fn(midi2_ci_state *state,
                              midi2_proc_write_fn write_fn, void *context) {
  if (state == NULL) return;
  state->write_fn = write_fn;
  state->write_context = context;
}

void midi2_ci_set_rng(midi2_ci_state *state,
                         midi2_ci_rng_fn rng, void *context) {
  if (state == NULL) return;
  state->rng         = rng;
  state->rng_context = context;
}

void midi2_ci_set_nak_on_unknown(midi2_ci_state *state, bool enabled) {
  if (state == NULL) return;
  state->nak_on_unknown = enabled;
}

void midi2_ci_set_auto_invalidate_on_collision(midi2_ci_state *state, bool enabled) {
  if (state == NULL) return;
  state->auto_invalidate_on_collision = enabled;
}

uint32_t midi2_ci_new_muid(midi2_ci_state *state) {
  uint32_t m;
  uint8_t tries = 0;
  if (state == NULL) return 0u;
  do {
    if (state->rng) {
      m = state->rng(state->rng_context) & 0x0FFFFFFFu;
    } else {
      /* Fallback: perturb current MUID. Better than returning a reserved
       * value. Real devices should always install an RNG. */
      m = (state->muid * 1103515245u + 12345u) & 0x0FFFFFFFu;
    }
    if (++tries > 8) break; /* avoid pathological loop */
  } while (m == 0u || m == 0x0FFFFFFFu);
  if (m == 0u || m == 0x0FFFFFFFu) m = 0x12345678u; /* hard fallback */
  state->muid = m;
  return m;
}

/*--------------------------------------------------------------------+
 * Profiles
 *--------------------------------------------------------------------*/
int midi2_ci_add_profile(midi2_ci_state *state, const uint8_t profile_id[5]) {
  if (state == NULL) return MIDI2_CI_ERR_NULL;
  if (state->profiles == NULL) return MIDI2_CI_ERR_NULL;
  if (state->profile_count >= state->profile_capacity) return MIDI2_CI_ERR_FULL;
  memcpy(state->profiles[state->profile_count], profile_id, 5);
  state->profile_count++;
  return MIDI2_CI_OK;
}

int midi2_ci_remove_profile(midi2_ci_state *state, const uint8_t profile_id[5]) {
  uint8_t i;
  if (state == NULL) return MIDI2_CI_ERR_NULL;
  for (i = 0; i < state->profile_count; i++) {
    if (memcmp(state->profiles[i], profile_id, 5) == 0) {
      uint8_t j;
      for (j = i; j < state->profile_count - 1; j++) {
        memcpy(state->profiles[j], state->profiles[j + 1], 5);
      }
      state->profile_count--;
      return MIDI2_CI_OK;
    }
  }
  return MIDI2_CI_ERR_NOT_FOUND;
}

/*--------------------------------------------------------------------+
 * Properties
 *--------------------------------------------------------------------*/
int midi2_ci_add_property_static(midi2_ci_state *state,
                                    const char *name, const char *value) {
  if (state == NULL) return MIDI2_CI_ERR_NULL;
  if (state->properties == NULL) return MIDI2_CI_ERR_NULL;
  if (state->property_count >= state->property_capacity) return MIDI2_CI_ERR_FULL;
  state->properties[state->property_count].name = name;
  state->properties[state->property_count].static_value = value;
  state->properties[state->property_count].getter = NULL;
  state->properties[state->property_count].setter = NULL;
  state->properties[state->property_count].subscribable = false; /* v0.3.0+ */
  state->property_count++;
  return MIDI2_CI_OK;
}

int midi2_ci_add_property_dynamic(midi2_ci_state *state,
                                     const char *name,
                                     midi2_ci_pe_getter getter,
                                     midi2_ci_pe_setter setter) {
  if (state == NULL) return MIDI2_CI_ERR_NULL;
  if (state->properties == NULL) return MIDI2_CI_ERR_NULL;
  if (state->property_count >= state->property_capacity) return MIDI2_CI_ERR_FULL;
  state->properties[state->property_count].name = name;
  state->properties[state->property_count].static_value = NULL;
  state->properties[state->property_count].getter = getter;
  state->properties[state->property_count].setter = setter;
  state->properties[state->property_count].subscribable = false; /* v0.3.0+ */
  state->property_count++;
  return MIDI2_CI_OK;
}

int midi2_ci_remove_property(midi2_ci_state *state, const char *name) {
  uint8_t i;
  if (state == NULL) return MIDI2_CI_ERR_NULL;
  if (name == NULL) return MIDI2_CI_ERR_NULL;
  for (i = 0; i < state->property_count; i++) {
    if (state->properties[i].name != NULL
        && strcmp(state->properties[i].name, name) == 0) {
      uint8_t j;
      for (j = i; j + 1 < state->property_count; j++) {
        state->properties[j] = state->properties[j + 1];
      }
      state->property_count--;
      return MIDI2_CI_OK;
    }
  }
  return MIDI2_CI_ERR_NOT_FOUND;
}

void midi2_ci_reset_profiles(midi2_ci_state *state) {
  if (state == NULL) return;
  state->profile_count = 0;
}

void midi2_ci_reset_properties(midi2_ci_state *state) {
  if (state == NULL) return;
  state->property_count = 0;
}

/*--------------------------------------------------------------------+
 * Subscribe / Notify (v0.3.0)
 *
 * Registry is caller-provided (state->subscribers). Each slot carries
 * a stable 36-char copy of the resource name so the responder does
 * not depend on app-owned string lifetimes.
 *--------------------------------------------------------------------*/
static int find_property_idx(const midi2_ci_state *state, const char *name) {
  uint8_t i;
  if (state == NULL || name == NULL) return -1;
  for (i = 0; i < state->property_count; i++) {
    if (state->properties[i].name != NULL
        && strcmp(state->properties[i].name, name) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static int find_subscriber_idx(const midi2_ci_state *state, uint32_t muid,
                                const char *name) {
  uint8_t i;
  if (state == NULL || state->subscribers == NULL || name == NULL) return -1;
  for (i = 0; i < state->subscriber_capacity; i++) {
    if (!state->subscribers[i].in_use) continue;
    if (state->subscribers[i].caller_muid != muid) continue;
    if (strncmp(state->subscribers[i].name_copy, name, 36) != 0) continue;
    return (int)i;
  }
  return -1;
}

int midi2_ci_pe_set_subscribable(midi2_ci_state *state, const char *name,
                                  bool subscribable) {
  int idx;
  if (state == NULL || name == NULL) return MIDI2_CI_ERR_NULL;
  idx = find_property_idx(state, name);
  if (idx < 0) return MIDI2_CI_ERR_NOT_FOUND;
  state->properties[idx].subscribable = subscribable;
  return MIDI2_CI_OK;
}

int midi2_ci_subscribe_add(midi2_ci_state *state, uint32_t caller_muid,
                            const char *resource_name) {
  uint8_t i;
  int pi;
  size_t n;
  if (state == NULL || resource_name == NULL) return MIDI2_CI_ERR_NULL;
  if (state->subscribers == NULL) return MIDI2_CI_ERR_FULL;
  pi = find_property_idx(state, resource_name);
  if (pi < 0) return MIDI2_CI_ERR_NOT_FOUND;
  if (!state->properties[pi].subscribable) return MIDI2_CI_ERR_NOT_FOUND;
  if (find_subscriber_idx(state, caller_muid, resource_name) >= 0) {
    return MIDI2_CI_OK; /* idempotent duplicate */
  }
  for (i = 0; i < state->subscriber_capacity; i++) {
    if (state->subscribers[i].in_use) continue;
    state->subscribers[i].caller_muid = caller_muid;
    n = strlen(resource_name);
    if (n > 36) n = 36;
    memcpy(state->subscribers[i].name_copy, resource_name, n);
    state->subscribers[i].name_copy[n] = '\0';
    state->subscribers[i].in_use = 1;
    state->subscriber_count++;
    return MIDI2_CI_OK;
  }
  return MIDI2_CI_ERR_FULL;
}

int midi2_ci_subscribe_remove(midi2_ci_state *state, uint32_t caller_muid,
                               const char *resource_name) {
  int idx;
  if (state == NULL || resource_name == NULL) return MIDI2_CI_ERR_NULL;
  idx = find_subscriber_idx(state, caller_muid, resource_name);
  if (idx < 0) return MIDI2_CI_ERR_NOT_FOUND;
  state->subscribers[idx].in_use = 0;
  state->subscribers[idx].caller_muid = 0;
  state->subscribers[idx].name_copy[0] = '\0';
  state->subscriber_count--;
  return MIDI2_CI_OK;
}

uint8_t midi2_ci_get_subscriber_count(const midi2_ci_state *state) {
  return (state == NULL) ? 0u : state->subscriber_count;
}

/*--------------------------------------------------------------------+
 * Internal: send SysEx via write function
 *--------------------------------------------------------------------*/
static void ci_send(midi2_ci_state *state, uint8_t group,
                     const uint8_t *data, uint16_t length) {
  if (state->write_fn) {
    midi2_proc_send_sysex7(group, data, length, state->write_fn, state->write_context);
  }
}

/*--------------------------------------------------------------------+
 * PE Notify fan-out (v0.3.0)
 *
 * Walks the subscriber registry and, for every slot matching the
 * resource name, emits a PE Notify frame carrying a minimal JSON
 * header `{"resource":"<name>"}`. The actual property value is not
 * embedded; consumers issue a PE Get to fetch the new value. Matches
 * the common M2-103 pattern for PE where Notify signals invalidation.
 *--------------------------------------------------------------------*/
int midi2_ci_notify_property_changed(midi2_ci_state *state,
                                      const char *resource_name) {
  /* JSON header template `{"resource":"<name>"}`. <name> is bounded by
   * MIDI2_CI_RESOURCE_NAME_MAX (36 per M2-105) and stored in the
   * subscriber's name_copy slot, so the worst-case header is
   * 13 (prefix) + 36 (name) + 2 (suffix) = 51 bytes. Buffer of 64 is
   * comfortable. No <stdio.h> dependency. */
  static const char HDR_PREFIX[] = "{\"resource\":\"";
  static const char HDR_SUFFIX[] = "\"}";
  uint8_t i;
  int pi;
  if (state == NULL || resource_name == NULL) return MIDI2_CI_ERR_NULL;
  pi = find_property_idx(state, resource_name);
  if (pi < 0) return MIDI2_CI_ERR_NOT_FOUND;
  if (state->write_fn == NULL || state->subscribers == NULL) return MIDI2_CI_OK;

  for (i = 0; i < state->subscriber_capacity; i++) {
    if (!state->subscribers[i].in_use) continue;
    if (strncmp(state->subscribers[i].name_copy, resource_name, 36) != 0) continue;

    uint8_t frame[128];
    uint8_t hdr[64];
    uint16_t hdr_n = 0;
    size_t name_len = strlen(state->subscribers[i].name_copy);
    if (name_len > 36u) name_len = 36u;

    memcpy(hdr + hdr_n, HDR_PREFIX, sizeof HDR_PREFIX - 1u);
    hdr_n = (uint16_t)(hdr_n + (sizeof HDR_PREFIX - 1u));
    memcpy(hdr + hdr_n, state->subscribers[i].name_copy, name_len);
    hdr_n = (uint16_t)(hdr_n + name_len);
    memcpy(hdr + hdr_n, HDR_SUFFIX, sizeof HDR_SUFFIX - 1u);
    hdr_n = (uint16_t)(hdr_n + (sizeof HDR_SUFFIX - 1u));

    uint16_t frame_n = midi2_ci_build_pe_notify(
        frame, CI_RESPONDER_VERSION,
        state->muid,
        state->subscribers[i].caller_muid,
        0 /* request_id */,
        hdr, hdr_n,
        1, 1,
        NULL, 0);
    if (frame_n == 0) continue;
    ci_send(state, 0 /* group */, frame, frame_n);
  }
  return MIDI2_CI_OK;
}

/*--------------------------------------------------------------------+
 * Discovery Reply -- uses midi2_ci_build_discovery_reply
 *--------------------------------------------------------------------*/
static bool ci_handle_discovery(midi2_ci_state *state, uint8_t group,
                                  const uint8_t *data, uint16_t length) {
  if (length < 13 || state->manufacturer_id == 0) return false;

  uint32_t src_muid = midi2_ci_get_src_muid(data);

  uint8_t reply[32];
  uint16_t reply_len = midi2_ci_build_discovery_reply(
      reply, CI_RESPONDER_VERSION, state->muid, src_muid,
      state->manufacturer_id, state->family_id, state->model_id,
      state->version_id, state->ci_cat, 512, 0, 0x7F);

  ci_send(state, group, reply, reply_len);
  return true;
}

/*--------------------------------------------------------------------+
 * Profile Inquiry Reply -- uses midi2_ci_build_profile_inquiry_reply
 *--------------------------------------------------------------------*/
static void ci_handle_profile_inquiry(midi2_ci_state *state, uint8_t group,
                                        const uint8_t *data, uint16_t length) {
  if (length < 13) return;

  uint32_t src_muid = midi2_ci_get_src_muid(data);

  uint8_t reply[256];
  uint16_t reply_len = midi2_ci_build_profile_inquiry_reply(
      reply, CI_RESPONDER_VERSION, state->muid, src_muid,
      midi2_ci_get_device_id(data),
      (const uint8_t (*)[5])state->profiles, state->profile_count,
      NULL, 0);

  ci_send(state, group, reply, reply_len);
}

/*--------------------------------------------------------------------+
 * PE Capability Reply -- uses midi2_ci_build_pe_capability_reply
 *
 * Parallels Profile Inquiry and PI Capability: without this, an
 * Initiator asking "do you do PE?" gets no answer and never tries
 * PE GET/SET. Advertises max_simultaneous=1 and PE v1.0 (basic).
 *--------------------------------------------------------------------*/
static void ci_handle_pe_capability(midi2_ci_state *state, uint8_t group,
                                      const uint8_t *data, uint16_t length) {
  if (length < 13) return;

  uint32_t src_muid = midi2_ci_get_src_muid(data);

  uint8_t reply[24];
  uint16_t reply_len = midi2_ci_build_pe_capability_reply(
      reply, CI_RESPONDER_VERSION, state->muid, src_muid,
      /*max_simultaneous*/ 1,
      /*pe_ver_major*/     1,
      /*pe_ver_minor*/     0);

  ci_send(state, group, reply, reply_len);
}

/*--------------------------------------------------------------------+
 * Property Exchange reply helpers (M2-105-UM)
 *
 * Every PE reply header is a JSON object carrying at least a "status".
 * An empty header makes an Initiator (e.g. the MIDI 2.0 Workbench) NAK with
 * "the first header property is not resource, status or command".
 *--------------------------------------------------------------------*/
static const char PE_HDR_OK[]  = "{\"status\":200}";
static const char PE_HDR_400[] = "{\"status\":400}";
static const char PE_HDR_404[] = "{\"status\":404}";

/* Assembled PE reply and body bounds, sized to the 512-byte Receivable Max
 * SysEx the responder advertises so a full DeviceInfo (manufacturer/family/
 * model/version plus the four ID arrays, ~200 bytes) round-trips without
 * truncation. REPLY_MAX must exceed BODY_MAX by the ~36-byte CI+PE framing. */
#define CI_PE_REPLY_MAX 512
#define CI_PE_BODY_MAX  448

/* Extract the "resource" string value from a PE inquiry header JSON. Returns a
 * pointer into hdr with *out_len set, or NULL if absent. Minimal scanner: no
 * escape handling (PE resource names are plain identifiers per M2-105). */
static const char *ci_pe_resource(const uint8_t *hdr, uint16_t hdr_len,
                                     uint16_t *out_len) {
  static const char KEY[] = "\"resource\"";
  const uint16_t klen = (uint16_t)(sizeof(KEY) - 1);
  uint16_t i;
  *out_len = 0;
  if (hdr == NULL || hdr_len < klen) return NULL;
  for (i = 0; (uint16_t)(i + klen) <= hdr_len; i++) {
    if (memcmp(hdr + i, KEY, klen) != 0) continue;
    i = (uint16_t)(i + klen);
    while (i < hdr_len && (hdr[i] == ' ' || hdr[i] == ':')) i++;
    if (i >= hdr_len || hdr[i] != '"') return NULL;
    i++;  /* opening quote */
    {
      uint16_t start = i;
      while (i < hdr_len && hdr[i] != '"') i++;
      *out_len = (uint16_t)(i - start);
      return (const char *)(hdr + start);
    }
  }
  return NULL;
}

/* Find a registered property whose name matches the requested resource. */
static const midi2_ci_property *ci_pe_find_property(const midi2_ci_state *state,
                                                       const char *res,
                                                       uint16_t res_len) {
  uint8_t i;
  for (i = 0; i < state->property_count; i++) {
    const char *name = state->properties[i].name;
    if (name != NULL && (uint16_t)strlen(name) == res_len
        && memcmp(name, res, res_len) == 0) {
      return &state->properties[i];
    }
  }
  return NULL;
}

/* Build the built-in ResourceList body: a JSON array of {"resource":"NAME"}
 * for every registered property. Emits only entries that fit whole, always
 * closing the array, so the result is valid JSON even when it overflows (a
 * truncated but well-formed list rather than an empty or half-written body).
 * Returns bytes written (>= 2, i.e. at least "[]"), or 0 if max < 2. */
static uint16_t ci_pe_build_resource_list(const midi2_ci_state *state,
                                            uint8_t *out, uint16_t max) {
  static const char PRE[] = "{\"resource\":\"";
  const uint16_t prelen = (uint16_t)(sizeof(PRE) - 1);
  uint16_t p = 0;
  uint8_t emitted = 0;
  uint8_t i;
  if (max < 2) return 0;  /* no room even for "[]" */
  out[p++] = '[';
  for (i = 0; i < state->property_count; i++) {
    const char *name = state->properties[i].name;
    uint16_t namelen, need, k;
    if (name == NULL) continue;
    namelen = (uint16_t)strlen(name);
    /* comma? + {"resource":" + name + "} , plus 1 reserved for closing ']'. */
    need = (uint16_t)((emitted > 0 ? 1u : 0u) + prelen + namelen + 2u + 1u);
    if ((uint16_t)(p + need) > max) break;  /* stop at the last whole entry */
    if (emitted > 0) out[p++] = ',';
    for (k = 0; k < prelen; k++)   out[p++] = (uint8_t)PRE[k];
    for (k = 0; k < namelen; k++)  out[p++] = (uint8_t)name[k];
    out[p++] = '"'; out[p++] = '}';
    emitted++;
  }
  out[p++] = ']';
  return p;
}

/* If body is a JSON array, count its top-level elements; returns 1 and sets
 * *count. Returns 0 for a non-array body (e.g. the DeviceInfo object). String-
 * and nesting-aware, so commas inside strings or nested objects/arrays are not
 * counted. An empty array "[]" yields count 0. */
static int ci_pe_array_count(const uint8_t *body, uint16_t len,
                               uint16_t *count) {
  uint16_t i = 0, n;
  int depth = 0, in_str = 0;
  *count = 0;
  while (i < len && (body[i] == ' ' || body[i] == '\t'
                     || body[i] == '\r' || body[i] == '\n')) i++;
  if (i >= len || body[i] != '[') return 0;
  for (i++; i < len && (body[i] == ' ' || body[i] == '\t'
                        || body[i] == '\r' || body[i] == '\n'); i++) {}
  if (i < len && body[i] == ']') return 1;  /* empty array -> 0 */
  n = 1;                                     /* at least one element */
  for (; i < len; i++) {
    uint8_t c = body[i];
    if (in_str) {
      if (c == '\\') { i++; continue; }      /* skip escaped char */
      if (c == '"') in_str = 0;
      continue;
    }
    if (c == '"') in_str = 1;
    else if (c == '{' || c == '[') depth++;
    else if (c == '}') depth--;
    else if (c == ']') { if (depth == 0) break; depth--; }
    else if (c == ',' && depth == 0) n++;
  }
  *count = n;
  return 1;
}

/* Build the success header: {"status":200} or, for a list resource,
 * {"status":200,"totalCount":N}. totalCount is required by M2-105 for
 * paginable resources (the Workbench warns without it). Returns bytes written;
 * buf must hold at least CI_PE_OK_HDR_MAX bytes. */
#define CI_PE_OK_HDR_MAX 40
static uint16_t ci_pe_ok_header(uint8_t *buf, int has_count, uint16_t count) {
  static const char HEAD[]  = "{\"status\":200";
  static const char TOTAL[] = ",\"totalCount\":";
  uint16_t p = 0, k;
  for (k = 0; k < (uint16_t)(sizeof(HEAD) - 1); k++) buf[p++] = (uint8_t)HEAD[k];
  if (has_count) {
    uint8_t tmp[5];
    uint8_t t = 0;
    for (k = 0; k < (uint16_t)(sizeof(TOTAL) - 1); k++) buf[p++] = (uint8_t)TOTAL[k];
    if (count == 0) { buf[p++] = '0'; }
    else {
      while (count > 0) { tmp[t++] = (uint8_t)('0' + (count % 10)); count /= 10; }
      while (t > 0)     { buf[p++] = tmp[--t]; }
    }
  }
  buf[p++] = '}';
  return p;
}

/*--------------------------------------------------------------------+
 * PE Get handler -- uses midi2_ci_build_pe_get_reply
 *
 * Matches the requested resource by name. Built-in "ResourceList" enumerates
 * the registered resources. Unknown resource -> {"status":404}. Every reply
 * carries a non-empty header; list resources also carry "totalCount".
 *--------------------------------------------------------------------*/
static void ci_handle_pe_get(midi2_ci_state *state, uint8_t group,
                               const uint8_t *data, uint16_t length) {
  /* Respond even with zero registered properties: a ResourceList Get still
   * gets an empty array, a named Get gets 404. The responder advertises PE,
   * so it must always answer. */
  if (length < 16) return;

  uint32_t src_muid   = midi2_ci_get_src_muid(data);
  uint8_t  request_id = midi2_ci_get_pe_request_id(data);
  uint16_t hdr_len    = midi2_ci_get_pe_header_len(data);
  const uint8_t *inq  = (length >= (uint16_t)(16 + hdr_len)) ? (data + 16) : NULL;

  uint16_t res_len = 0;
  const char *res = ci_pe_resource(inq, hdr_len, &res_len);

  uint8_t reply[CI_PE_REPLY_MAX];
  uint16_t reply_len;

  /* Built-in ResourceList: enumerate the registered resources. An
   * app-registered "ResourceList" property takes precedence (served by the
   * named lookup below), so devices can publish entries that carry a schema
   * for custom X- resources, as M2-105 requires. */
  if (res != NULL && res_len == 12 && memcmp(res, "ResourceList", 12) == 0
      && !ci_pe_find_property(state, res, res_len)) {
    uint8_t body[CI_PE_BODY_MAX];
    uint16_t body_len = ci_pe_build_resource_list(state, body, sizeof(body));
    uint8_t hdr[CI_PE_OK_HDR_MAX];
    uint16_t total = 0;
    (void)ci_pe_array_count(body, body_len, &total);
    reply_len = midi2_ci_build_pe_get_reply(
        reply, CI_RESPONDER_VERSION, state->muid, src_muid, request_id,
        hdr, ci_pe_ok_header(hdr, 1, total),
        1, 1, body, body_len);
    ci_send(state, group, reply, reply_len);
    return;
  }

  /* Named resource lookup. */
  if (res != NULL) {
    uint8_t i;
    for (i = 0; i < state->property_count; i++) {
      const char *name = state->properties[i].name;
      const char *value;
      uint16_t val_len;
      if (name == NULL || (uint16_t)strlen(name) != res_len
          || memcmp(name, res, res_len) != 0) continue;

      value = state->properties[i].getter
          ? state->properties[i].getter(name, state->context)
          : state->properties[i].static_value;
      if (value == NULL) break;  /* resource exists but has no value -> 404 */

      uint8_t hdr[CI_PE_OK_HDR_MAX];
      uint16_t total = 0;
      int is_list;
      val_len = (uint16_t)strlen(value);
      if (val_len > CI_PE_BODY_MAX) val_len = CI_PE_BODY_MAX;

      /* List resources (array body) carry totalCount; objects (DeviceInfo) do
       * not. Count on the possibly-truncated body so it matches what is sent. */
      is_list = ci_pe_array_count((const uint8_t *)value, val_len, &total);
      reply_len = midi2_ci_build_pe_get_reply(
          reply, CI_RESPONDER_VERSION, state->muid, src_muid, request_id,
          hdr, ci_pe_ok_header(hdr, is_list, total),
          1, 1, (const uint8_t *)value, val_len);
      ci_send(state, group, reply, reply_len);
      return;
    }
  }

  /* Unknown or unspecified resource -> status 404. */
  reply_len = midi2_ci_build_pe_get_reply(
      reply, CI_RESPONDER_VERSION, state->muid, src_muid, request_id,
      (const uint8_t *)PE_HDR_404, (uint16_t)(sizeof(PE_HDR_404) - 1),
      1, 1, NULL, 0);
  ci_send(state, group, reply, reply_len);
}

/*--------------------------------------------------------------------+
 * PE Set handler -- uses midi2_ci_build_pe_set_reply
 *
 * Matches the requested resource by name and passes the request body to that
 * resource's setter. Status reflects reality: 200 on success, 404 for an
 * unknown or non-settable resource, 400 when the setter rejects the value.
 *--------------------------------------------------------------------*/
static void ci_handle_pe_set(midi2_ci_state *state, uint8_t group,
                               const uint8_t *data, uint16_t length) {
  if (length < 16) return;

  uint32_t src_muid   = midi2_ci_get_src_muid(data);
  uint8_t  request_id = midi2_ci_get_pe_request_id(data);
  uint16_t hdr_len    = midi2_ci_get_pe_header_len(data);
  const uint8_t *inq  = (length >= (uint16_t)(16 + hdr_len)) ? (data + 16) : NULL;

  uint16_t res_len = 0;
  const char *res = ci_pe_resource(inq, hdr_len, &res_len);

  const char *status  = PE_HDR_404;  /* default: no matching settable resource */
  uint16_t    status_len = (uint16_t)(sizeof(PE_HDR_404) - 1);

  if (res != NULL) {
    uint8_t i;
    for (i = 0; i < state->property_count; i++) {
      const char *name = state->properties[i].name;
      if (name == NULL || (uint16_t)strlen(name) != res_len
          || memcmp(name, res, res_len) != 0) continue;

      if (state->properties[i].setter == NULL) break;  /* exists, read-only -> 404 */

      /* Extract the (single-chunk) request body and pass it to the setter as a
       * NUL-terminated string. Body follows header + num_chunks + this_chunk +
       * body_len (M2-105 PE data layout). */
      {
        uint16_t off = (uint16_t)(16 + hdr_len);
        char valbuf[CI_PE_BODY_MAX + 1];
        uint16_t body_len = 0;
        const uint8_t *body = NULL;
        bool ok;

        if (length >= (uint16_t)(off + 6)) {
          body_len = midi2_ci_read_14(&data[off + 4]);
          body = &data[off + 6];
          if (length < (uint16_t)(off + 6 + body_len)) body_len = 0;
        }
        if (body_len > CI_PE_BODY_MAX) body_len = CI_PE_BODY_MAX;
        if (body_len > 0) memcpy(valbuf, body, body_len);
        valbuf[body_len] = '\0';

        ok = state->properties[i].setter(name, valbuf, state->context);
        if (ok) { status = PE_HDR_OK;  status_len = (uint16_t)(sizeof(PE_HDR_OK)  - 1); }
        else    { status = PE_HDR_400; status_len = (uint16_t)(sizeof(PE_HDR_400) - 1); }
      }
      break;
    }
  }

  {
    uint8_t reply[64];
    uint16_t reply_len = midi2_ci_build_pe_set_reply(
        reply, CI_RESPONDER_VERSION, state->muid, src_muid,
        request_id, (const uint8_t *)status, status_len);
    ci_send(state, group, reply, reply_len);
  }
}

/*--------------------------------------------------------------------+
 * Invalidate MUID handler (M2-101-UM §3.5 + Appendix E)
 *
 * If the message's target MUID matches ours, regenerate it via the
 * installed RNG. Without an RNG the request is silently ignored (v0.2.3
 * behavior preserved).
 *--------------------------------------------------------------------*/

/*--------------------------------------------------------------------+
 * Re-announce with a fresh MUID (M2-101 §5.9 / Workbench ci1.2)
 *
 * After the MUID changes (Invalidate MUID or collision), the initiator
 * only learns the new value if the device initiates a new Discovery.
 * Silence here strands every open transaction on a dead MUID.
 *--------------------------------------------------------------------*/
static void ci_reannounce_discovery(midi2_ci_state *state, uint8_t group) {
  if (state->write_fn == NULL) return;
  uint8_t disc[40];
  uint16_t len = midi2_ci_build_discovery(
      disc, CI_RESPONDER_VERSION, state->muid,
      state->manufacturer_id, state->family_id, state->model_id,
      state->version_id, state->ci_cat, 512, 0);
  ci_send(state, group, disc, len);
}

static void ci_handle_invalidate_muid(midi2_ci_state *state, uint8_t group,
                                         const uint8_t *data, uint16_t length) {
  if (length < 17) return;
  if (state->rng == NULL) return;
  uint32_t target = midi2_ci_get_target_muid(data);
  if (target != state->muid) return;
  midi2_ci_new_muid(state);
  ci_reannounce_discovery(state, group);
}

/*--------------------------------------------------------------------+
 * MUID collision detection (M2-101-UM Appendix E §2)
 *
 * Any inbound CI message whose src_muid matches ours means a peer is
 * using our MUID. Resolution: pick a new MUID and broadcast Invalidate
 * for the old value. No-op without an RNG.
 *--------------------------------------------------------------------*/
static void ci_check_muid_collision(midi2_ci_state *state, uint8_t group,
                                       uint32_t peer_src_muid) {
  if (state->rng == NULL) return;
  if (peer_src_muid != state->muid) return;
  uint32_t old = state->muid;
  midi2_ci_new_muid(state);
  if (state->write_fn == NULL) return;
  if (!state->auto_invalidate_on_collision) return;
  uint8_t buf[24];
  uint16_t len = midi2_ci_build_invalidate_muid(
      buf, CI_RESPONDER_VERSION, state->muid, old);
  ci_send(state, group, buf, len);
  ci_reannounce_discovery(state, group);
}

/*--------------------------------------------------------------------+
 * NAK builder (M2-101-UM Appendix E)
 *
 * Build a Sub-ID#2 0x7F NAK with status NOT_SUPPORTED for a given
 * original sub-id. Used when nak_on_unknown is enabled.
 *--------------------------------------------------------------------*/
static void ci_send_nak_not_supported(midi2_ci_state *state, uint8_t group,
                                         const uint8_t *data,
                                         uint8_t orig_sub_id) {
  if (state->write_fn == NULL) return;
  uint32_t src_muid = midi2_ci_get_src_muid(data);
  uint8_t device_id = midi2_ci_get_device_id(data);
  uint8_t buf[32];
  uint16_t len = midi2_ci_build_nak(
      buf, CI_RESPONDER_VERSION, state->muid, src_muid, device_id,
      orig_sub_id, MIDI2_CI_NAK_NOT_SUPPORTED, 0,
      NULL, 0, NULL);
  ci_send(state, group, buf, len);
}

/*--------------------------------------------------------------------+
 * Process Inquiry handler -- uses midi2_ci_build_pi_capability_reply
 *--------------------------------------------------------------------*/
static void ci_handle_process_inquiry(midi2_ci_state *state, uint8_t group,
                                        const uint8_t *data, uint16_t length) {
  if (length < 13) return;

  uint32_t src_muid = midi2_ci_get_src_muid(data);

  uint8_t reply[16];
  uint16_t reply_len = midi2_ci_build_pi_capability_reply(
      reply, CI_RESPONDER_VERSION, state->muid, src_muid, 0x01);

  ci_send(state, group, reply, reply_len);
}

/*--------------------------------------------------------------------+
 * Profile Set On/Off handler
 *
 * The responder publishes fixed, always-on profiles: a listed profile cannot
 * be turned off. Per the Profile Configuration rules a Set Profile On for a
 * profile that can be turned on, and a Set Profile Off for a profile that
 * cannot be turned off while currently on, both reply with a Profile Enabled
 * Report. A Set message for a profile the responder does not publish is NAKed.
 *--------------------------------------------------------------------*/
static bool ci_profile_listed(const midi2_ci_state *state,
                                const uint8_t profile_id[5]) {
  uint8_t i;
  for (i = 0; i < state->profile_count; i++) {
    if (memcmp(state->profiles[i], profile_id, 5) == 0) return true;
  }
  return false;
}

static void ci_handle_set_profile(midi2_ci_state *state, uint8_t group,
                                     const uint8_t *data, uint16_t length,
                                     bool set_on) {
  if (length < 18) return;  /* 13-byte header + 5-byte profile id */

  uint8_t        device_id  = midi2_ci_get_device_id(data);
  const uint8_t *profile_id = data + 13;

  uint8_t  reply[32];
  uint16_t reply_len;

  if (!ci_profile_listed(state, profile_id)) {
    ci_send_nak_not_supported(state, group, data,
                              set_on ? MIDI2_CI_SET_PROFILE_ON
                                     : MIDI2_CI_SET_PROFILE_OFF);
    return;
  }

  /* Listed and always-on: enabled either way. Echo the requested channel count
   * from a Set Profile On (v2, at offset 18); a Set Profile Off carries none. */
  uint16_t num_channels = 0;
  if (set_on && length >= 20) {
    num_channels = midi2_ci_read_14(&data[18]);
  }
  reply_len = midi2_ci_build_profile_enabled(
      reply, CI_RESPONDER_VERSION, state->muid, device_id,
      profile_id, num_channels);
  ci_send(state, group, reply, reply_len);
}

/*--------------------------------------------------------------------+
 * Process Inquiry: MIDI Message Report handler
 *
 * The responder keeps no live channel state, so it reports no messages: it
 * answers with a Reply to MIDI Message Report declaring an empty set, then
 * End of MIDI Message Report. A request addressed to a single MIDI channel the
 * device does not use is NAKed (channel not in use); Group (0x7E) and Function
 * Block (0x7F) requests are answered once.
 *--------------------------------------------------------------------*/
static void ci_handle_pi_midi_report(midi2_ci_state *state, uint8_t group,
                                        const uint8_t *data, uint16_t length) {
  if (length < 18) return;  /* header + MDC + 4 bitmap bytes */

  uint32_t src_muid  = midi2_ci_get_src_muid(data);
  uint8_t  device_id = midi2_ci_get_device_id(data);

  if (device_id <= 0x0F && device_id != 0x00) {
    ci_send_nak_not_supported(state, group, data, MIDI2_CI_PI_MIDI_REPORT);
    return;
  }

  uint8_t  buf[32];
  uint16_t len;

  len = midi2_ci_build_pi_midi_report_reply(
      buf, CI_RESPONDER_VERSION, state->muid, src_muid, device_id,
      /*system*/ 0, /*reserved*/ 0, /*channel_ctrl*/ 0, /*note_data*/ 0);
  ci_send(state, group, buf, len);

  len = midi2_ci_build_pi_midi_report_end(
      buf, CI_RESPONDER_VERSION, state->muid, src_muid, device_id);
  ci_send(state, group, buf, len);
}

/*--------------------------------------------------------------------+
 * Process incoming SysEx
 *--------------------------------------------------------------------*/
bool midi2_ci_process_sysex(midi2_ci_state *state,
                               uint8_t group, const uint8_t *data, uint16_t length) {
  if (state == NULL) return false;
  if (!midi2_ci_is_ci(data, length)) return false;

  /* Every inbound CI message is an opportunity to detect MUID collisions.
   * Do this before dispatching so a reply (if any) carries the new MUID. */
  ci_check_muid_collision(state, group, midi2_ci_get_src_muid(data));

  uint8_t sub_id = midi2_ci_get_sub_id(data);
  switch (sub_id) {
    case MIDI2_CI_DISCOVERY:
      return ci_handle_discovery(state, group, data, length);

    case MIDI2_CI_INVALIDATE_MUID:
      ci_handle_invalidate_muid(state, group, data, length);
      return true;

    case MIDI2_CI_PROFILE_INQUIRY:
      ci_handle_profile_inquiry(state, group, data, length);
      return true;

    case MIDI2_CI_SET_PROFILE_ON:
      ci_handle_set_profile(state, group, data, length, true);
      return true;

    case MIDI2_CI_SET_PROFILE_OFF:
      ci_handle_set_profile(state, group, data, length, false);
      return true;

    case MIDI2_CI_PE_CAPABILITY:
      ci_handle_pe_capability(state, group, data, length);
      return true;

    case MIDI2_CI_PE_GET:
      ci_handle_pe_get(state, group, data, length);
      return true;

    case MIDI2_CI_PE_SET:
      ci_handle_pe_set(state, group, data, length);
      return true;

    case MIDI2_CI_PI_CAPABILITY:
      ci_handle_process_inquiry(state, group, data, length);
      return true;

    case MIDI2_CI_PI_MIDI_REPORT:
      ci_handle_pi_midi_report(state, group, data, length);
      return true;

    default:
      /* Appendix E: "Be able to send a NAK message when appropriate."
       * When nak_on_unknown is set, reply with Sub-ID#2 0x7F
       * status NOT_SUPPORTED. Otherwise the v0.2.3 behavior (silent
       * fall-through to return false) is preserved. */
      if (state->nak_on_unknown) {
        ci_send_nak_not_supported(state, group, data, sub_id);
        return true;
      }
      return false;
  }
}

#endif /* MIDI2_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* MIDI2_H */

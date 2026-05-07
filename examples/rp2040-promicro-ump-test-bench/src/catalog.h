/*
 * catalog.h, deterministic UMP catalog for the test bench.
 *
 * The catalog is a fixed table of 101 entries (indices 0..100) covering
 * every UMP category exercised by the Windows MIDI Services consumer
 * side. Entries follow the order of section 5 of
 * docs/plans/2026-04-28-ump-test-bench-rp2040.md:
 *
 *   §5.1  Flex Data 0x00 (Setup and Performance)   indices  0..18
 *   §5.2  Flex Data 0x01 (Metadata Text)           indices 19..29
 *   §5.3  Flex Data 0x02 (Performance Text)        indices 30..34
 *   §5.4  MIDI 2.0 Channel Voice (mt 0x4)          indices 35..58
 *   §5.5  MIDI 1.0 Channel Voice (mt 0x2)          indices 59..63
 *   §5.6  System Common / Real-Time (mt 0x1)       indices 64..73
 *   §5.7  Data Messages (SysEx7 + SysEx8)          indices 74..81
 *   §5.8  UMP Stream (mt 0xF)                      indices 82..93
 *   §5.9  Utility (mt 0x0)                         indices 94..98
 *   §5.10 Edge cases                               indices 99..100
 *
 * The catalog API takes a midi2::m2device and emits a single entry per
 * call. Each entry is responsible for emitting its UMPs and printing
 * one EMIT line per emitted UMP on stdout (UART GP0/GP1 @ 115200 8N1).
 *
 * Indices that are not yet implemented print one stub EMIT line with
 * label "TODO" and four zero words; the catalog will get filled in
 * sequentially during the next phase.
 */
#pragma once

#include <cstdint>

#include "midi2_cpp.h"

namespace ump_test_bench {

// Number of entries in the catalog. Stays at 101 (0..100) per the spec.
constexpr uint8_t kCatalogSize = 101;

// Emit the entry at the given index. Index in [0, kCatalogSize); out of
// range is a no-op with a warning printed on stdout. Returns true if
// the entry was emitted (or stub-printed), false if idx was out of
// range.
bool catalogEmit(uint8_t idx, midi2::m2device& midi);

// Convenience: emit every entry in order, separated by tx_pause_ms
// milliseconds. Used by the boot auto-emit. Caller must guarantee that
// midi.isMounted() is true and midi.altSetting() == 1 throughout the
// span (the catalog itself does not poll mount state).
void catalogEmitAll(midi2::m2device& midi, uint32_t tx_pause_ms);

}  // namespace ump_test_bench

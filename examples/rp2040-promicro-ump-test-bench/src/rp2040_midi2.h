/*
 * rp2040_midi2.h, public API of the rp2040-promicro-ump-test-bench board core.
 *
 * Same shape as the rp2040-midi2 sibling: the application layer (here,
 * the catalog emitter + trigger handler) consumes this header and
 * never touches tud_*, pico_*, or any USB symbol directly. After init,
 * the m2device + m2ci instances are wired to the platform USB stack
 * through midi2cpp's five public hooks (setWriteFn, feedRx, setNowFn,
 * setMounted, CI::setRngFn). The app then registers callbacks, sends
 * UMPs, and calls task() in the main loop.
 */
#pragma once

#include "midi2cpp.h"

namespace rp2040_midi2 {

// Boots board_init + tusb_init, sets up USB MIDI 2.0 device class, and
// wires the five midi2cpp platform hooks into the supplied m2device /
// m2ci. After this returns, the app can register callbacks, send UMPs,
// and call task() in its main loop.
//
// Must be called once at startup, before any midi.send* / ci.* calls.
void init(midi2::m2device& midi, midi2::m2ci& ci);

// Drains the USB stack (tud_task) and pumps any received UMP words into
// midi.feedRx. Call every iteration of the main loop.
void task(midi2::m2device& midi);

// Bench-only: write raw UMP words straight to TinyUSB. Used by the
// catalog for entries that midi2cpp does not expose a sender for
// (Endpoint Discovery, Stream Config Request, FB Discovery) and for
// the deliberate edge cases (reserved-bit-set, unassigned status).
// No-op when the device is not mounted or the alt setting is not 1.
void pumpRaw(const uint32_t* words, uint32_t count);

// Bench-only stress flood: emit up to maxPackets MIDI 2.0 note-ons back to
// back at the USB line rate, each carrying a monotonic 16-bit sequence in its
// velocity (starting at startSeq, wrapping at 65536). Stops early when the TX
// FIFO has no room for a whole packet (the UMP write is all-or-nothing per
// packet, so nothing tears). Returns the number actually written, so the
// caller advances its sequence and total by the return value and the on-wire
// sequence stays contiguous (any gap the host sees is then loss on the host).
uint32_t floodBurst(uint8_t group, uint8_t channel, uint8_t note,
                    uint16_t startSeq, uint32_t maxPackets);

}  // namespace rp2040_midi2

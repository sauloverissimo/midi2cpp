#pragma once

#include <cstdint>

namespace midi2 {

// ============================================================================
// RxRing, single-producer / single-consumer ring of raw UMP packets.
//
// This is the "stream core" that decouples the USB receive path from message
// processing. The producer (feedRx, on the platform RX path) copies each UMP
// packet in and returns in O(1), never blocking and never decoding. The
// consumer (task(), on the main loop) drains the ring and runs the heavy
// decode/dispatch. With the producer and consumer each owning one index, no
// lock is needed on a 32-bit MCU.
//
// Why it matters: a bare-metal USB host (e.g. TinyUSB) delivers RX from within
// its task/IRQ. Doing the full UMP decode there starves the USB service and
// the non-overwritable RX FIFO silently truncates under a burst. Enqueue-then-
// drain keeps the RX path short so nothing is lost; size N for the worst burst.
//
// Drop policy: when the ring is full, push() refuses the NEW packet (drop-
// newest, so the producer never touches the consumer's index) and counts it in
// dropped(). A clean run keeps dropped() == 0.
//
// One slot is kept empty to tell full from empty, so usable capacity is N-1.
// ============================================================================
template <uint16_t N>
class RxRing {
public:
    struct Slot {
        uint32_t ump[4];   // raw UMP words as received (unused slots = 0)
        uint8_t  idx;      // source device index (host is multi-device)
        uint8_t  words;    // valid words, 1..4
    };

    RxRing() : head_(0), tail_(0), dropped_(0) {}

    // Producer: enqueue one UMP packet (1..4 words). O(1), never blocks.
    // Returns false and counts the drop when the ring is full (drop-newest).
    bool push(uint8_t idx, const uint32_t* w, uint8_t wc) {
        if (!w || wc == 0 || wc > 4) return false;
        const uint16_t next = advance_(head_);
        if (next == tail_) {              // full: refuse the new packet
            ++dropped_;
            return false;
        }
        Slot& s = ring_[head_];
        for (uint8_t i = 0; i < 4; ++i) s.ump[i] = (i < wc) ? w[i] : 0u;
        s.idx   = idx;
        s.words = wc;
        head_ = next;                     // publish last
        return true;
    }

    // Consumer: pull the oldest waiting packet into out. Returns false when
    // empty. FIFO: packets come out in the exact order they arrived.
    bool pop(Slot& out) {
        if (tail_ == head_) return false;
        out = ring_[tail_];
        tail_ = advance_(tail_);
        return true;
    }

    bool     empty() const   { return tail_ == head_; }
    uint32_t dropped() const { return dropped_; }

    static constexpr uint16_t capacity() { return (uint16_t)(N - 1); }

private:
    static uint16_t advance_(uint16_t i) { return (uint16_t)((i + 1) % N); }

    Slot ring_[N];
    volatile uint16_t head_;   // written only by push (producer)
    volatile uint16_t tail_;   // written only by pop (consumer)
    uint32_t dropped_;
};

}  // namespace midi2

/*
 * board_midi2.cpp: generic board core (TinyUSB <-> midi2cpp glue).
 *
 * Owns: Pico SDK board init, TinyUSB device init (with MIDI 2.0 class
 * driver), and the wiring between TinyUSB and midi2cpp via the five
 * public hooks. The application layer never sees any of this; it only
 * sees `midi2::m2device` + `midi2::m2ci` objects that are already alive.
 */
#include "board_midi2.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/rand.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "class/midi/midi2_device.h"

#include "midi2_rx_ring.h"

namespace midi2_board {

midi2::m2device* g_midi = nullptr;

// SPSC ring: tud_midi2_rx_cb (producer, tud_task context) -> task() (consumer).
// Move fast in the callback, decode in the loop. 64 slots fit a paginated PE GET.
static midi2::RxRing<64> g_rx_ring;

namespace {

// Outbound UMP, invoked by the library for every sendXxx() and the
// JR heartbeat. Forwards to TinyUSB MIDI 2.0 stream write.
void platform_write_fn(const uint32_t* words, size_t count) {
    // UMP writes require mounted + Alt Setting 1; the driver returns 0 otherwise.
    if (!tud_midi2_n_mounted(0) || tud_midi2_n_alt_setting(0) != 1) return;
    // Full TX FIFO mid-burst is backpressure, not an error: pump tud_task()
    // and retry (bounded). Dropping would truncate a multi-packet reply.
    uint32_t off = 0;
    uint32_t spin = 0;
    while (off < count) {
        off += tud_midi2_n_ump_write(0, words + off, (uint32_t)(count - off));
        if (off >= count) break;
        tud_task();
        if (++spin > 20000) return;   // bounded: host gone
    }
}

// Monotonic millisecond clock used by the JR heartbeat.
uint32_t platform_now_fn() {
    return (uint32_t)(time_us_64() / 1000ULL);
}

// Entropy source for MUID generation / regeneration.
uint32_t platform_rng_fn() {
    return get_rand_32();
}

}  // namespace

void init(midi2::m2device& midi, midi2::m2ci& ci) {
    // Board + TinyUSB device bring-up. Pico SDK's stdio_init_all() is
    // intentionally NOT called here; UART vs USB CDC stdio is the app's
    // call (USB is dedicated to MIDI in this core, but the app may want
    // UART debug print).
    board_init();

    g_midi = &midi;

    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
    board_init_after_tusb();

    // Wire the midi2cpp platform hooks; mounted/alt are bootstrap values,
    // kept in sync by the polling in task().
    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setMounted(false);
    midi.setAltSetting(0);
    ci.setRngFn(platform_rng_fn);
}

void pumpRaw(const uint32_t* words, uint32_t count) {
    if (!tud_midi2_n_mounted(0)) return;
    if (tud_midi2_n_alt_setting(0) != 1) return;
    tud_midi2_n_ump_write(0, words, count);
}

void task(midi2::m2device& midi) {
    // Decode + MIDI-CI replies run here, where the reply path can pump tud_task().
    {
        midi2::RxRing<64>::Slot slot;
        while (g_rx_ring.pop(slot)) {
            midi.feedRx(slot.ump, slot.words);
        }
    }
    // Poll mount/alt state (set_itf_cb only fires on alt-setting changes).
    bool mounted = tud_midi2_n_mounted(0);
    midi.setMounted(mounted);
    midi.setAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    // Drain the USB stack itself.
    tud_task();

    // RX drain happens in tud_midi2_rx_cb below.

    // Library housekeeping (heartbeat, deferred sends).
    midi.task();
}

}  // namespace midi2_board

/* TinyUSB MIDI 2.0 callbacks (midi2_device.h API). Minimal stubs; state
 * propagation is handled by polling in task(). */
extern "C" {

// Pump UMP words from TinyUSB into the library's dispatcher.
void tud_midi2_rx_cb(uint8_t itf) {
    // Drain the class FIFO the moment data lands, as the upstream
    // midi2_device example does: an immediate drain lets the driver
    // re-arm the OUT endpoint right away, so a multi-packet inbound run
    // (e.g. a paginated PE GET) streams in without long NAK windows.
    // This callback runs inside tud_task(), so it only MOVES packets
    // into the SPSC ring; decode happens in task() on the main loop.
    uint32_t buf[4];
    uint32_t n;
    while ((n = tud_midi2_n_ump_read(itf, buf, 4)) > 0) {
        midi2_board::g_rx_ring.push(0, buf, (uint8_t)n);
    }
}

// Fires on alt-setting change (host issued SET_INTERFACE). State is
// already propagated via polling in task(); no extra work needed here.
void tud_midi2_set_itf_cb(uint8_t itf, uint8_t alt) {
    (void)itf;
    (void)alt;
}

// Class-specific control requests the driver doesn't handle internally.
// Returning false lets TinyUSB stall any unsupported request, which is
// the safe default.
bool tud_midi2_get_req_itf_cb(uint8_t rhport,
                               const tusb_control_request_t* request) {
    (void)rhport;
    (void)request;
    return false;
}

// One bidirectional Function Block over Group 1. The #3738 built-in Stream
// responder derives the FB Info (direction + group span) from this GTB.
static const uint8_t k_gtb_desc[] = {
    TUD_MIDI2_GTB_HEADER(1),
    TUD_MIDI2_GTB_BLOCK(/*id*/ 1, MIDI2_GTB_BIDIRECTIONAL,
                        /*first_group*/ 0, /*num_groups*/ 1, /*stridx*/ 0),
};

const uint8_t* tud_midi2_gtb_desc_cb(uint8_t itf, uint16_t* len) {
    (void)itf;
    *len = (uint16_t)sizeof(k_gtb_desc);
    return k_gtb_desc;
}

const char* tud_midi2_fb_name_cb(uint8_t itf, uint8_t fb_idx) {
    (void)itf;
    return (fb_idx == 0) ? CFG_TUD_MIDI2_EP_NAME : "";
}

}  // extern "C"

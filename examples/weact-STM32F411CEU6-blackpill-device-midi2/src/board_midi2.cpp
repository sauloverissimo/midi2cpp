/*
 * board_midi2.cpp: generic board core (TinyUSB <-> midi2cpp glue).
 * WeAct Studio STM32F411CEU6 BlackPill (V2.0 / V3.0).
 *
 * Owns: board_init (STM32F411 clocks via the BSP RCC config, 25 MHz HSE
 * to 84 MHz SYSCLK / 48 MHz USB, OTG_FS pins PA11/PA12), TinyUSB device
 * stack init (with MIDI 2.0 class driver), the wiring between TinyUSB
 * and midi2cpp via the public hooks, and the on-board LED on PC13
 * (active-low; the BSP board_led_write handles the inversion).
 */
#include "board_midi2.h"

#include <cstdlib>

#include "bsp/board_api.h"
#include "tusb.h"

#include "midi2_rx_ring.h"

namespace midi2_board {

midi2::m2device* g_midi = nullptr;

// SPSC ring: tud_midi2_rx_cb (producer, tud_task context) -> task() (consumer).
// Move fast in the callback, decode in the loop. 64 slots fit a paginated PE GET.
static midi2::RxRing<64> g_rx_ring;

namespace {

// Outbound UMP, invoked by the library for every sendXxx and the JR
// heartbeat. Forwards to TinyUSB MIDI 2.0 stream write.
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

// Monotonic millisecond clock for the JR heartbeat. The TinyUSB BSP
// provides tusb_time_millis_api() backed by SysTick on the STM32F4.
uint32_t platform_now_fn() {
    return (uint32_t)tusb_time_millis_api();
}

// Entropy source for MUID. The STM32F411 does not carry the hardware
// RNG peripheral (only the larger STM32F4 parts such as F407/F42x do),
// so we use libc rand() seeded from tusb_time_millis_api() at init.
// Quality is poor but adequate for the educational MUID use case;
// production firmware should swap in a stronger entropy source.
uint32_t platform_rng_fn() {
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}

}  // namespace

void init(midi2::m2device& midi, midi2::m2ci& ci) {
    board_init();

    g_midi = &midi;

    tusb_rhport_init_t dev_init = {};
    dev_init.role  = TUSB_ROLE_DEVICE;
    dev_init.speed = TUSB_SPEED_AUTO;
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    // Seed the libc PRNG from the board millisecond counter at boot.
    // First call to platform_rng_fn happens on first MUID generation.
    srand(tusb_time_millis_api() ^ 0xA5A5A5A5u);

    // Wire the midi2cpp platform hooks; mounted/alt are bootstrap values,
    // kept in sync by the polling in task().
    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setMounted(false);
    midi.setAltSetting(0);
    ci.setRngFn(platform_rng_fn);
}

void task(midi2::m2device& midi) {
    // Decode + MIDI-CI replies run here, where the reply path can pump tud_task().
    {
        midi2::RxRing<64>::Slot slot;
        while (g_rx_ring.pop(slot)) {
            midi.feedRx(slot.ump, slot.words);
        }
    }
    // Refresh USB lifecycle state into the library every iteration.
    bool mounted = tud_midi2_n_mounted(0);
    midi.setMounted(mounted);
    midi.setAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    // RX drain happens in tud_midi2_rx_cb below.

    // Drain the USB stack.
    tud_task();

    // Library housekeeping (heartbeat, deferred sends).
    midi.task();
}

void led_show_mounted(bool mounted) {
    board_led_write(mounted);
}

}  // namespace midi2_board

/* TinyUSB MIDI 2.0 callbacks (midi2_device.h API). Minimal stubs; state
 * propagation is handled by polling in task(). */
extern "C" {

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

void tud_midi2_set_itf_cb(uint8_t itf, uint8_t alt) {
    (void)itf;
    (void)alt;
}

bool tud_midi2_get_req_itf_cb(uint8_t rhport,
                              const tusb_control_request_t* request) {
    (void)rhport;
    (void)request;
    return false;
}

// One bidirectional Function Block over Group 1. The #3738 built-in
// responder reads the FB Info (direction + group span) from this GTB.
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

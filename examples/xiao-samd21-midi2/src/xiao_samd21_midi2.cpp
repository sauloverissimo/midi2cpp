/*
 * xiao_samd21_midi2.cpp, board core implementation for the Seeed XIAO
 * SAMD21.
 *
 * Owns: board_init (SAMD21 clocks, USB pins via TinyUSB BSP), TinyUSB
 * device stack init (with MIDI 2.0 class driver from PR #3571), the
 * wiring between TinyUSB and midi2cpp via the public hooks, and the
 * on-board yellow LED on PA17.
 */
#include "xiao_samd21_midi2.h"

#include <cstdlib>

#include "bsp/board_api.h"
#include "tusb.h"

namespace xiao_samd21_midi2 {

namespace {

// Outbound UMP, invoked by the library for every sendXxx and the JR
// heartbeat. Forwards to TinyUSB MIDI 2.0 stream write.
void platform_write_fn(const uint32_t* words, size_t count) {
    if (!tud_midi2_n_mounted(0)) return;
    if (tud_midi2_n_alt_setting(0) != 1) return;  // 1 = UMP MIDI 2.0
    tud_midi2_n_ump_write(0, words, (uint32_t)count);
}

// Monotonic millisecond clock for the JR heartbeat. The TinyUSB BSP
// provides tusb_time_millis_api() backed by SysTick on the SAMD21.
uint32_t platform_now_fn() {
    return (uint32_t)tusb_time_millis_api();
}

// Entropy source for MUID. The SAMD21 does not have a hardware TRNG,
// so we use libc rand() seeded from tusb_time_millis_api() at init. Quality is
// poor but adequate for the educational MUID use case; production
// firmware should swap in a stronger entropy source.
uint32_t platform_rng_fn() {
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}

}  // namespace

void init(midi2::m2device& midi, midi2::m2ci& ci) {
    board_init();

    tusb_rhport_init_t dev_init = {};
    dev_init.role  = TUSB_ROLE_DEVICE;
    dev_init.speed = TUSB_SPEED_AUTO;
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    // Seed the libc PRNG from the board millisecond counter at boot.
    // First call to platform_rng_fn happens on first MUID generation.
    srand(tusb_time_millis_api() ^ 0xA5A5A5A5u);

    // Wire the five midi2cpp platform hooks. Mounted + alt setting
    // are kept in sync with TinyUSB by the polling loop in task(); the
    // initial values here are bootstrap defaults.
    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setMounted(false);
    midi.setAltSetting(0);
    ci.setRngFn(platform_rng_fn);
}

void task(midi2::m2device& midi) {
    // Pump TinyUSB events.
    tud_task();

    // Refresh USB lifecycle state into the library every iteration.
    bool mounted = tud_midi2_n_mounted(0);
    midi.setMounted(mounted);
    midi.setAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    // Drain RX, pump any UMP words into the dispatcher.
    if (mounted) {
        uint32_t buf[8];
        for (;;) {
            uint32_t n = tud_midi2_n_ump_read(0, buf, 8);
            if (n == 0) break;
            midi.feedRx(buf, n);
        }
    }

    // Library housekeeping (heartbeat, deferred sends).
    midi.task();
}

void led_show_mounted(bool mounted) {
    board_led_write(mounted);
}

}  // namespace xiao_samd21_midi2

/*--------------------------------------------------------------------+
 * TinyUSB MIDI 2.0 callbacks, required hooks per midi2_device.h API.
 *
 * The driver references these as caller-supplied with no weak default
 * upstream. Polling done in xiao_samd21_midi2::task() handles state
 * propagation, so these stubs do nothing.
 *--------------------------------------------------------------------*/
extern "C" {

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

}  // extern "C"

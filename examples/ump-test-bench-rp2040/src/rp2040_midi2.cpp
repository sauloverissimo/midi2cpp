/*
 * rp2040_midi2.cpp, board core implementation.
 *
 * Owns: Pico SDK board init, TinyUSB device init (with MIDI 2.0 class
 * driver from PR #3571), and the wiring between TinyUSB and midi2_cpp
 * via the five public hooks. The application layer never sees any of
 * this; it only sees `midi2::m2device` + `midi2::m2ci` objects that
 * are already alive.
 */
#include "rp2040_midi2.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/rand.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "class/midi/midi2_device.h"

namespace rp2040_midi2 {

namespace {

// Outbound UMP, invoked by the library for every sendXxx() and the
// JR heartbeat. Forwards to TinyUSB MIDI 2.0 stream write.
void platform_write_fn(const uint32_t* words, size_t count) {
    if (!tud_midi2_n_mounted(0)) return;
    if (tud_midi2_n_alt_setting(0) != 1) return;  // 1 = MIDI 2.0 stream
    tud_midi2_n_ump_write(0, words, (uint32_t)count);
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

    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
    board_init_after_tusb();

    // Wire the five midi2_cpp platform hooks. From now on, the app
    // operates entirely through midi/ci; the platform layer below is
    // invisible. Mounted/alt state are kept in sync with TinyUSB by the
    // polling loop in task(), so the initial values here are just
    // bootstrap defaults.
    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setMounted(false);
    midi.setAltSetting(0);
    ci.setRngFn(platform_rng_fn);
}

void task(midi2::m2device& midi) {
    // Refresh USB lifecycle state (mount + alt setting) into the library
    // every loop iteration. tud_midi2_set_itf_cb fires on alt-setting
    // changes; polling here handles the initial mount path uniformly.
    bool mounted = tud_midi2_n_mounted(0);
    midi.setMounted(mounted);
    midi.setAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    // Drain the USB stack itself.
    tud_task();

    // Drain RX, pump any UMP words from TinyUSB into the library's
    // dispatcher. Bounded buffer; the inner while loop handles bursts.
    if (mounted) {
        uint32_t buf[16];
        for (;;) {
            uint32_t n = tud_midi2_n_ump_read(0, buf, 16);
            if (n == 0) break;
            midi.feedRx(buf, n);
        }
    }

    // Library housekeeping (heartbeat, deferred sends).
    midi.task();
}

}  // namespace rp2040_midi2

/*--------------------------------------------------------------------+
 * TinyUSB MIDI 2.0 callbacks, required hooks per midi2_device.h API.
 *
 * The TinyUSB MIDI 2.0 driver references these as caller-supplied (no
 * weak default upstream). We provide the minimum the driver needs; the
 * polling done in rp2040_midi2::task() handles state propagation, so
 * these stubs do nothing. Future apps that need finer control can
 * replace this translation unit in their own build.
 *--------------------------------------------------------------------*/
extern "C" {

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

}  // extern "C"

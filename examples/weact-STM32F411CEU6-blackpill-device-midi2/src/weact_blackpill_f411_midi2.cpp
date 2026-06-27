/*
 * weact_blackpill_f411_midi2.cpp, board core implementation for the
 * WeAct Studio STM32F411CEU6 BlackPill (V2.0 / V3.0).
 *
 * Owns: board_init (STM32F411 clocks via the BSP RCC config, 25 MHz HSE
 * to 84 MHz SYSCLK / 48 MHz USB, OTG_FS pins PA11/PA12), TinyUSB device
 * stack init (with MIDI 2.0 class driver), the wiring between TinyUSB
 * and midi2cpp via the public hooks, and the on-board LED on PC13
 * (active-low; the BSP board_led_write handles the inversion).
 */
#include "weact_blackpill_f411_midi2.h"

#include <cstdlib>

#include "bsp/board_api.h"
#include "tusb.h"

namespace weact_blackpill_f411_midi2 {

midi2::m2device* g_midi = nullptr;

namespace {

// Outbound UMP, invoked by the library for every sendXxx and the JR
// heartbeat. Forwards to TinyUSB MIDI 2.0 stream write.
void platform_write_fn(const uint32_t* words, size_t count) {
    if (!tud_midi2_n_mounted(0)) return;
    if (tud_midi2_n_alt_setting(0) != 1) return;  // 1 = UMP MIDI 2.0
    tud_midi2_n_ump_write(0, words, (uint32_t)count);
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

}  // namespace weact_blackpill_f411_midi2

/*--------------------------------------------------------------------+
 * TinyUSB MIDI 2.0 callbacks, required hooks per midi2_device.h API.
 *
 * The driver references these as caller-supplied with no weak default
 * upstream. Polling done in weact_blackpill_f411_midi2::task() handles
 * state propagation, so these stubs do nothing.
 *--------------------------------------------------------------------*/
extern "C" {

void tud_midi2_rx_cb(uint8_t itf) {
    if (!weact_blackpill_f411_midi2::g_midi) return;
    uint32_t buf[8];
    for (;;) {
        uint32_t n = tud_midi2_n_ump_read(itf, buf, 8);
        if (n == 0) break;
        weact_blackpill_f411_midi2::g_midi->feedRx(buf, n);
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

}  // extern "C"

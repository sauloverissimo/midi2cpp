/*
 * nrf52840_promicro_midi2.cpp, board core implementation for Pro Micro
 * nRF52840 class boards (Nice!Nano and clones).
 *
 * Owns: board_init (nRF52840 clocks, USB pins via TinyUSB BSP), TinyUSB
 * device stack init (with MIDI 2.0 class driver), the wiring between
 * TinyUSB and midi2cpp via the public hooks, and the on-board LED via
 * the TinyUSB BSP (P1.15 on Feather Express; not visible on most
 * generic Pro Micro clones).
 */
#include "nrf52840_promicro_midi2.h"

#include <cstdlib>

#include "bsp/board_api.h"
#include "tusb.h"

namespace nrf52840_promicro_midi2 {

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
// provides tusb_time_millis_api() backed by the nRF52 RTC1 in the BSP.
uint32_t platform_now_fn() {
    return (uint32_t)tusb_time_millis_api();
}

// Entropy source for MUID. The nRF52840 has a hardware TRNG, but
// reaching it from this layer requires nrfx and a SoftDevice-aware
// guard (the Adafruit bootloader leaves S140 v6 resident). For the
// educational MUID use case we use libc rand() seeded from
// tusb_time_millis_api at init; production firmware should swap in
// the TRNG via sd_rand_application_vector_get when SoftDevice is
// enabled, or NRF_RNG direct access when it is not.
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

    srand(tusb_time_millis_api() ^ 0xA5A5A5A5u);

    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setMounted(false);
    midi.setAltSetting(0);
    ci.setRngFn(platform_rng_fn);
}

void task(midi2::m2device& midi) {
    tud_task();

    bool mounted = tud_midi2_n_mounted(0);
    midi.setMounted(mounted);
    midi.setAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    // RX drain happens in tud_midi2_rx_cb below.

    midi.task();
}

void led_show_mounted(bool mounted) {
    board_led_write(mounted);
}

}  // namespace nrf52840_promicro_midi2

/*--------------------------------------------------------------------+
 * TinyUSB MIDI 2.0 callbacks, required hooks per midi2_device.h API.
 *
 * The driver references these as caller-supplied with no weak default
 * upstream. Polling done in nrf52840_promicro_midi2::task() handles
 * state propagation, so these stubs do nothing.
 *--------------------------------------------------------------------*/
extern "C" {

void tud_midi2_rx_cb(uint8_t itf) {
    if (!nrf52840_promicro_midi2::g_midi) return;
    uint32_t buf[8];
    for (;;) {
        uint32_t n = tud_midi2_n_ump_read(itf, buf, 8);
        if (n == 0) break;
        nrf52840_promicro_midi2::g_midi->feedRx(buf, n);
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
    return (fb_idx == 0) ? "Main" : "";
}

}  // extern "C"

/*
 * t_display_s3_midi2.cpp, board core implementation.
 *
 * Owns: ESP32-S3 USB-OTG PHY init (USB_PHY_TARGET_INT), TinyUSB device
 * driver install (with MIDI 2.0 class driver from PR #3571), the wiring
 * between TinyUSB and midi2_cpp via the five public hooks, and the
 * ST7789 1.9" 320x170 piano display via the piano_display component.
 * The application layer only sees `midi2::m2device` + `midi2::m2ci`
 * objects that are already alive.
 */
#include "t_display_s3_midi2.h"

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"

#include "piano_display.h"

namespace t_display_s3_midi2 {

namespace {

constexpr const char* TAG = "t-display-s3-midi2";

// Outbound UMP, invoked by the library for the JR heartbeat and for any
// device-side response (Stream Discovery replies, MIDI-CI Discovery
// reply, NAK, etc.). The recipe is a receiver: it does not send notes,
// but it MUST respond to host queries to behave like a well-formed
// MIDI 2.0 device.
void platform_write_fn(const uint32_t* words, size_t count) {
    if (!tud_midi2_n_mounted(0)) return;
    if (tud_midi2_n_alt_setting(0) != 1) return;  // 1 = MIDI 2.0 stream
    tud_midi2_n_ump_write(0, words, (uint32_t)count);
}

// Monotonic millisecond clock used by the JR heartbeat.
uint32_t platform_now_fn() {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

// Entropy source for MUID generation / regeneration.
uint32_t platform_rng_fn() {
    return esp_random();
}

void usb_phy_init() {
    usb_phy_handle_t phy = nullptr;
    usb_phy_config_t cfg = {};
    cfg.controller        = USB_PHY_CTRL_OTG;
    cfg.target            = USB_PHY_TARGET_INT;
    cfg.otg_mode          = USB_OTG_MODE_DEVICE;
    cfg.otg_speed         = USB_PHY_SPEED_FULL;
    ESP_ERROR_CHECK(usb_new_phy(&cfg, &phy));
    ESP_LOGI(TAG, "USB-OTG internal PHY ready (device, full speed)");
}

}  // namespace

// Dedicated FreeRTOS task that runs the TinyUSB device stack.
void tinyusb_task(void* arg) {
    (void)arg;
    while (true) {
        tud_task();
        vTaskDelay(1);
    }
}

// Dedicated FreeRTOS task that drives the on-board piano UI at ~60 fps.
// Renders the active-note state owned by the piano_display module.
void piano_render_task(void* arg) {
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(16);  // ~62.5 fps
    while (true) {
        piano_display::render_frame();
        vTaskDelay(period);
    }
}

void init(midi2::m2device& midi, midi2::m2ci& ci) {
    usb_phy_init();

    // ST7789 + piano roll. Allocates the LovyanGFX driver, full-screen
    // sprite (320x170 16bpp = 108 KB in PSRAM), and the active-note
    // state buffer.
    piano_display::init();
    piano_display::set_status("waiting for host...");

    // TinyUSB device init, direct API of the PR #3571 fork.
    tusb_rhport_init_t dev_init = {};
    dev_init.role  = TUSB_ROLE_DEVICE;
    dev_init.speed = TUSB_SPEED_AUTO;
    if (!tusb_init(BOARD_TUD_RHPORT, &dev_init)) {
        ESP_LOGE(TAG, "tusb_init failed");
        return;
    }
    xTaskCreatePinnedToCore(tinyusb_task, "tinyusb",
                            /*stack*/ 8192,
                            /*param*/ nullptr,
                            /*prio*/ 5,
                            /*handle*/ nullptr,
                            /*core*/ 0);
    ESP_LOGI(TAG, "TinyUSB device task started");

    xTaskCreatePinnedToCore(piano_render_task, "piano",
                            /*stack*/ 4096,
                            /*param*/ nullptr,
                            /*prio*/ 3,
                            /*handle*/ nullptr,
                            /*core*/ 1);
    ESP_LOGI(TAG, "Piano render task started");

    // Wire the five midi2_cpp platform hooks.
    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setMounted(false);
    midi.setAltSetting(0);
    ci.setRngFn(platform_rng_fn);
}

void task(midi2::m2device& midi) {
    bool mounted = tud_midi2_n_mounted(0);
    midi.setMounted(mounted);
    midi.setAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    if (mounted) {
        uint32_t buf[16];
        for (;;) {
            uint32_t n = tud_midi2_n_ump_read(0, buf, 16);
            if (n == 0) break;
            midi.feedRx(buf, n);
        }
    }

    midi.task();
}

void show_mounted(bool mounted) {
    piano_display::set_status(mounted ? "host connected (UMP alt 1)"
                                      : "waiting for host...");
}

}  // namespace t_display_s3_midi2

/*--------------------------------------------------------------------+
 * TinyUSB MIDI 2.0 callbacks, required hooks per midi2_device.h API.
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

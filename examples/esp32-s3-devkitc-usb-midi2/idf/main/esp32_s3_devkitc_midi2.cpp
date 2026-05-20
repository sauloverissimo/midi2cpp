/*
 * esp32_s3_devkitc_midi2.cpp, board core implementation.
 *
 * Owns: ESP32-S3 USB-OTG PHY init (USB_PHY_TARGET_INT), TinyUSB device
 * driver install (with MIDI 2.0 class driver), the wiring between
 * TinyUSB and midi2cpp via the five public hooks, and the on-board RGB
 * LED indicator on GPIO48. The application layer only sees
 * `midi2::m2device` + `midi2::m2ci` objects that are already alive.
 */
#include "esp32_s3_devkitc_midi2.h"

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"
#include "led_strip.h"

namespace esp32_s3_devkitc_midi2 {

midi2::m2device* g_midi = nullptr;

namespace {

constexpr const char* TAG = "esp32-s3-midi2";

// On-board RGB LED. ESP32-S3-DevKitC-1 v1.1 ties the WS2812 data line
// to GPIO48 by default. Older v1.0 silkscreen used GPIO38; the user can
// override at build time with -DLED_STRIP_GPIO=<n>.
#ifndef LED_STRIP_GPIO
#define LED_STRIP_GPIO  48
#endif

led_strip_handle_t g_led_strip = nullptr;

// Outbound UMP, invoked by the library for every sendXxx() and the JR
// heartbeat. Forwards to TinyUSB MIDI 2.0 stream write.
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

void led_strip_init() {
    led_strip_config_t led_cfg = {};
    led_cfg.strip_gpio_num = LED_STRIP_GPIO;
    led_cfg.max_leds       = 1;
    led_cfg.led_model      = LED_MODEL_WS2812;
    // Note: color_component_format field exists in led_strip >= 3.0;
    // 2.5 uses GRB by default for WS2812.

    led_strip_rmt_config_t rmt_cfg = {};
    rmt_cfg.clk_src       = RMT_CLK_SRC_DEFAULT;
    rmt_cfg.resolution_hz = 10 * 1000 * 1000;

    if (led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &g_led_strip) != ESP_OK) {
        ESP_LOGW(TAG, "led_strip init failed; mounted indicator disabled");
        g_led_strip = nullptr;
        return;
    }
    led_strip_clear(g_led_strip);
}

}  // namespace

// Dedicated FreeRTOS task that runs the TinyUSB device stack.
// Equivalent to what the Espressif esp_tinyusb wrapper would do, kept
// inline so this recipe owns the full bring-up without an extra
// component dependency.
void tinyusb_task(void* arg) {
    (void)arg;
    while (true) {
        tud_task();
        vTaskDelay(1);
    }
}

void init(midi2::m2device& midi, midi2::m2ci& ci) {
    usb_phy_init();
    led_strip_init();

    g_midi = &midi;

    // TinyUSB device init. The MIDI 2.0 class driver registers itself
    // when CFG_TUD_MIDI2 is enabled in tusb_config.h.
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

    // Wire the five midi2cpp platform hooks. From now on, the app
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
    // Refresh USB lifecycle state (mount + alt setting) into the
    // library every iteration. tud_midi2_set_itf_cb fires on alt
    // changes; polling here handles the initial mount path uniformly.
    bool mounted = tud_midi2_n_mounted(0);
    midi.setMounted(mounted);
    midi.setAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    // RX drain happens in tud_midi2_rx_cb below.

    // Library housekeeping (heartbeat, deferred sends).
    midi.task();
}

void led_show_mounted(bool mounted) {
    if (g_led_strip == nullptr) return;
    if (mounted) {
        led_strip_set_pixel(g_led_strip, 0, /*r*/ 0, /*g*/ 24, /*b*/ 0);
    } else {
        led_strip_set_pixel(g_led_strip, 0, /*r*/ 24, /*g*/ 0, /*b*/ 0);
    }
    led_strip_refresh(g_led_strip);
}

}  // namespace esp32_s3_devkitc_midi2

/*--------------------------------------------------------------------+
 * TinyUSB MIDI 2.0 callbacks, required hooks per midi2_device.h API.
 *
 * The TinyUSB MIDI 2.0 driver references these as caller-supplied (no
 * weak default upstream). We provide the minimum the driver needs; the
 * polling done in esp32_s3_devkitc_midi2::task() handles state
 * propagation, so these stubs do nothing. Future apps that need finer
 * control can replace this translation unit in their own build.
 *--------------------------------------------------------------------*/
extern "C" {

void tud_midi2_rx_cb(uint8_t itf) {
    if (!esp32_s3_devkitc_midi2::g_midi) return;
    uint32_t buf[16];
    for (;;) {
        uint32_t n = tud_midi2_n_ump_read(itf, buf, 16);
        if (n == 0) break;
        esp32_s3_devkitc_midi2::g_midi->feedRx(buf, n);
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

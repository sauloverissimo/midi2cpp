/*
 * arduino_nano_esp32_midi2.cpp, board core implementation.
 *
 * Owns: ESP32-S3 USB-OTG PHY init (USB_PHY_TARGET_INT), TinyUSB device
 * driver install (with MIDI 2.0 class driver), the wiring between
 * TinyUSB and midi2cpp via the five public hooks, and a single GPIO
 * LED indicator (LED_BUILTIN on most Arduino Nano ESP32 / Pro Micro
 * ESP32-S3 form factors). The application layer only sees
 * `midi2::m2device` + `midi2::m2ci` objects that are already alive.
 */
#include "arduino_nano_esp32_midi2.h"

#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"
#include "driver/gpio.h"

namespace arduino_nano_esp32_midi2 {

midi2::m2device* g_midi = nullptr;

namespace {

constexpr const char* TAG = "arduino-nano-esp32-midi2";

// LED_BUILTIN on the Arduino Nano ESP32 is GPIO48 (orange, single
// channel). Override at build time with -DLED_BUILTIN_GPIO=<n> if your
// clone wires it differently.
#ifndef LED_BUILTIN_GPIO
#define LED_BUILTIN_GPIO  48
#endif

void platform_write_fn(const uint32_t* words, size_t count) {
    if (!tud_midi2_n_mounted(0)) return;
    if (tud_midi2_n_alt_setting(0) != 1) return;
    tud_midi2_n_ump_write(0, words, (uint32_t)count);
}

uint32_t platform_now_fn() {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

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

void led_init() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << LED_BUILTIN_GPIO);
    cfg.mode         = GPIO_MODE_OUTPUT;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)LED_BUILTIN_GPIO, 0);
}

}  // namespace

void tinyusb_task(void* arg) {
    (void)arg;
    while (true) {
        tud_task();
        vTaskDelay(1);
    }
}

void init(midi2::m2device& midi, midi2::m2ci& ci) {
    usb_phy_init();
    led_init();

    g_midi = &midi;

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

    // RX drain happens in tud_midi2_rx_cb below.

    midi.task();
}

void led_show_mounted(bool mounted) {
    gpio_set_level((gpio_num_t)LED_BUILTIN_GPIO, mounted ? 1 : 0);
}

}  // namespace arduino_nano_esp32_midi2

extern "C" {

void tud_midi2_rx_cb(uint8_t itf) {
    if (!arduino_nano_esp32_midi2::g_midi) return;
    uint32_t buf[16];
    for (;;) {
        uint32_t n = tud_midi2_n_ump_read(itf, buf, 16);
        if (n == 0) break;
        arduino_nano_esp32_midi2::g_midi->feedRx(buf, n);
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

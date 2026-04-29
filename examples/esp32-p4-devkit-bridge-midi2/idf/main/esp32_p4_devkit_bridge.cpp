/*
 * esp32_p4_devkit_bridge.cpp, board core implementation (dual-stack).
 *
 * Owns:
 *   - ESP32-P4 USB-OTG dual PHY init: UTMI for host (rhport 1, USB-A
 *     jacks) + INT for device (rhport 0, USB-Device USB-C jack)
 *   - The LP_SYS.usb_ctrl PHY swap on the device side (mandatory on
 *     the Waveshare WIFI6 dev kit, see esp32-p4-devkit-usb-midi2 D-024)
 *   - TinyUSB device + host driver install
 *   - Wiring between TinyUSB and midi2_cpp via m2device, m2ci, m2host
 *     hooks
 *
 * The application layer only sees the three m2 objects after init().
 */
#include "esp32_p4_devkit_bridge.h"

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_private/usb_phy.h"
#include "soc/lp_system_struct.h"
#include "tusb.h"
#include "class/midi/midi2_host.h"

namespace esp32_p4_devkit_bridge {

constexpr const char* TAG = "esp32-p4-bridge";
constexpr uint8_t TUD_RHPORT = 0;
constexpr uint8_t TUH_RHPORT = 1;

// Globals for C-callable callbacks.
midi2::m2host*    g_host = nullptr;
uint16_t          g_bcdMSC[midi2::Host::MAX_DEVICES] = {0x0200, 0x0200, 0x0200, 0x0200};

namespace {

void platform_dev_write_fn(const uint32_t* words, size_t count) {
    if (!tud_midi2_n_mounted(0)) return;
    if (tud_midi2_n_alt_setting(0) != 1) return;
    tud_midi2_n_ump_write(0, words, (uint32_t)count);
}

void platform_host_write_fn(uint8_t idx, const uint32_t* words, size_t count) {
    if (!tuh_midi2_mounted(idx)) return;
    tuh_midi2_ump_write(idx, words, (uint32_t)count);
    tuh_midi2_write_flush(idx);
}

uint32_t platform_now_fn() {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

uint32_t platform_rng_fn() {
    return esp_random();
}

void usb_phy_init_host() {
    usb_phy_handle_t phy = nullptr;
    usb_phy_config_t cfg = {};
    cfg.controller        = USB_PHY_CTRL_OTG;
    cfg.target            = USB_PHY_TARGET_UTMI;
    cfg.otg_mode          = USB_OTG_MODE_HOST;
    cfg.otg_speed         = USB_PHY_SPEED_UNDEFINED;
    ESP_ERROR_CHECK(usb_new_phy(&cfg, &phy));
    ESP_LOGI(TAG, "Host UTMI PHY ready (rhport %u)", (unsigned)TUH_RHPORT);
}

void usb_phy_init_device() {
    // Mandatory PHY swap on the P4 USB-Device USB-C jack (D-024).
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 1;
    LP_SYS.usb_ctrl.sw_usb_phy_sel    = 1;

    usb_phy_handle_t phy = nullptr;
    usb_phy_config_t cfg = {};
    cfg.controller        = USB_PHY_CTRL_OTG;
    cfg.target            = USB_PHY_TARGET_INT;
    cfg.otg_mode          = USB_OTG_MODE_DEVICE;
    cfg.otg_speed         = USB_PHY_SPEED_FULL;
    ESP_ERROR_CHECK(usb_new_phy(&cfg, &phy));
    ESP_LOGI(TAG, "Device INT PHY ready, full speed (rhport %u)", (unsigned)TUD_RHPORT);
}

void tinyusb_host_task(void* arg) {
    (void)arg;
    while (true) {
        tuh_task();
        vTaskDelay(1);
    }
}

void tinyusb_device_task(void* arg) {
    (void)arg;
    while (true) {
        tud_task();
        vTaskDelay(1);
    }
}

}  // namespace

void init(midi2::m2device& midi, midi2::m2ci& ci, midi2::m2host& host) {
    g_host = &host;

    // m2device + m2ci hooks
    midi.setWriteFn(platform_dev_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setMounted(false);
    midi.setAltSetting(0);
    ci.setRngFn(platform_rng_fn);

    // m2host hooks
    host.setWriteFn(platform_host_write_fn);
    host.setNowFn(platform_now_fn);
    host.setRngFn(platform_rng_fn);
    host.begin();

    // Bring up host PHY first (UTMI), then device PHY (INT with swap).
    // Order matters: UTMI does not depend on the swap; doing host first
    // keeps the LP_SYS bits clean for the second call.
    usb_phy_init_host();
    usb_phy_init_device();

    // Install both stacks. rhport order: device first to settle USB-OTG
    // INT controller before host UTMI takes over its enumeration sweep.
    tusb_rhport_init_t dev_init = {};
    dev_init.role  = TUSB_ROLE_DEVICE;
    dev_init.speed = TUSB_SPEED_AUTO;
    if (!tusb_rhport_init(TUD_RHPORT, &dev_init)) {
        ESP_LOGE(TAG, "tusb_rhport_init(device) failed");
        return;
    }

    tusb_rhport_init_t host_init = {};
    host_init.role  = TUSB_ROLE_HOST;
    host_init.speed = TUSB_SPEED_AUTO;
    if (!tusb_rhport_init(TUH_RHPORT, &host_init)) {
        ESP_LOGE(TAG, "tusb_rhport_init(host) failed");
        return;
    }

    xTaskCreatePinnedToCore(tinyusb_device_task, "tinyusb_dev",
                            8192, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(tinyusb_host_task, "tinyusb_host",
                            8192, nullptr, 5, nullptr, 1);
    ESP_LOGI(TAG, "Both TinyUSB tasks started (device on core 0, host on core 1)");
}

void task(midi2::m2device& midi, midi2::m2host& host) {
    // Device side: refresh mount/alt and drain RX
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

    // Host side: drain RX per device idx
    for (uint8_t idx = 0; idx < midi2::Host::MAX_DEVICES; ++idx) {
        if (!tuh_midi2_mounted(idx)) continue;
        uint32_t buf[16];
        for (;;) {
            uint32_t n = tuh_midi2_ump_read(idx, buf, 16);
            if (n == 0) break;
            host.feedRx(idx, buf, n);
        }
    }
    host.task();
}

}  // namespace esp32_p4_devkit_bridge

/*--------------------------------------------------------------------+
 * TinyUSB device callbacks (rhport 0).
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

/*--------------------------------------------------------------------+
 * TinyUSB host callbacks (rhport 1).
 *--------------------------------------------------------------------*/
extern "C" {

void tuh_midi2_descriptor_cb(uint8_t idx,
                              const tuh_midi2_descriptor_cb_t* d) {
    if (idx >= midi2::Host::MAX_DEVICES || !d) return;
    esp32_p4_devkit_bridge::g_bcdMSC[idx] =
        ((uint16_t)d->bcdMSC_hi << 8) | (uint16_t)d->bcdMSC_lo;
}

void tuh_midi2_mount_cb(uint8_t idx, const tuh_midi2_mount_cb_t* m) {
    if (!esp32_p4_devkit_bridge::g_host) return;
    uint16_t bcd = (idx < midi2::Host::MAX_DEVICES)
                     ? esp32_p4_devkit_bridge::g_bcdMSC[idx] : 0x0200;
    esp32_p4_devkit_bridge::g_host->notifyDeviceMounted(
        idx, m->protocol_version, m->rx_cable_count,
        m->alt_setting_active, bcd);
}

void tuh_midi2_rx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {}
void tuh_midi2_tx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {}

void tuh_midi2_umount_cb(uint8_t idx) {
    if (!esp32_p4_devkit_bridge::g_host) return;
    esp32_p4_devkit_bridge::g_host->notifyDeviceUnmounted(idx);
}

}  // extern "C"

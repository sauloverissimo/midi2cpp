/*
 * esp32_p4_devkit_host.cpp, board core implementation (host role).
 *
 * Owns: ESP32-P4 USB-OTG UTMI PHY init (host role, high speed),
 * TinyUSB host driver install (with MIDI 2.0 host class), and the
 * wiring between TinyUSB host and midi2cpp via the m2host hooks. The
 * application layer only sees `midi2::m2host` after init().
 *
 * The Waveshare ESP32-P4-WIFI6-DEV-KIT routes the UTMI PHY to the two
 * USB-A jacks. No software PHY swap is required for the host role
 * (the swap is only needed when bringing OTG_FS to the USB-C jack on
 * the device side, see the device-only recipe).
 */
#include "esp32_p4_devkit_host.h"

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"
#include "class/midi/midi2_host.h"

namespace esp32_p4_devkit_host {

constexpr const char* TAG = "esp32-p4-host";
constexpr uint8_t TUH_RHPORT = 1;   // matches BOARD_TUH_RHPORT in tusb_config.h

// We hold the m2host reference for the duration of the program so the
// TinyUSB host C-style callbacks can route into the right object. Set
// in init(); the program is the lifetime.
midi2::m2host* g_midi = nullptr;

// bcdMSC arrives via descriptor_cb but mount_cb does not carry it.
// Stash per-idx so the mount handler can pass the right value to
// m2host. Default 0x0200 covers spec v2 if descriptor_cb is somehow
// skipped.
uint16_t g_bcdMSC[midi2::Host::MAX_DEVICES] = {0x0200, 0x0200, 0x0200, 0x0200};

namespace {

void platform_write_fn(uint8_t idx, const uint32_t* words, size_t count) {
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

void usb_phy_init() {
    // P4 host role uses the UTMI PHY directly. No LP_SYS swap needed
    // (that swap is only relevant when sending OTG_FS to the USB-C
    // device jack; UTMI is a separate controller and pin pair, and the
    // Waveshare board routes those pins to the USB-A jacks).
    usb_phy_handle_t phy = nullptr;
    usb_phy_config_t cfg = {};
    cfg.controller        = USB_PHY_CTRL_OTG;
    cfg.target            = USB_PHY_TARGET_UTMI;
    cfg.otg_mode          = USB_OTG_MODE_HOST;
    cfg.otg_speed         = USB_PHY_SPEED_UNDEFINED;  // P4 UTMI accepts any
    ESP_ERROR_CHECK(usb_new_phy(&cfg, &phy));
    ESP_LOGI(TAG, "USB-OTG UTMI PHY ready (host, rhport %u)",
             (unsigned)TUH_RHPORT);
}

void tinyusb_host_task(void* arg) {
    (void)arg;
    while (true) {
        tuh_task();
        vTaskDelay(1);
    }
}

}  // namespace

void init(midi2::m2host& midi) {
    g_midi = &midi;

    std::printf("[init] step 1: m2host hooks\r\n"); std::fflush(stdout);
    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setRngFn(platform_rng_fn);
    midi.begin();

    std::printf("[init] step 2: usb_phy_init\r\n"); std::fflush(stdout);
    usb_phy_init();

    std::printf("[init] step 3: tusb_rhport_init host\r\n"); std::fflush(stdout);
    tusb_rhport_init_t host_init = {};
    host_init.role  = TUSB_ROLE_HOST;
    host_init.speed = TUSB_SPEED_AUTO;
    if (!tusb_rhport_init(TUH_RHPORT, &host_init)) {
        ESP_LOGE(TAG, "tusb_rhport_init(host) failed");
        return;
    }

    std::printf("[init] step 4: spawn host task\r\n"); std::fflush(stdout);
    xTaskCreatePinnedToCore(tinyusb_host_task, "tinyusb_host",
                            /*stack*/ 8192,
                            /*param*/ nullptr,
                            /*prio*/ 5,
                            /*handle*/ nullptr,
                            /*core*/ 0);
    std::printf("[init] step 5: done\r\n"); std::fflush(stdout);
    ESP_LOGI(TAG, "TinyUSB host task started");
}

// UMP word count per Message Type (top nibble of word 0). Indexed by MT.
static const uint8_t kMtWordCount[16] = {
    1, 1, 1, 2,   // 0,1,2,3
    2, 4, 1, 1,   // 4,5,6,7
    2, 2, 2, 3,   // 8,9,A,B
    3, 4, 4, 4    // C,D,E,F
};

void task(midi2::m2host& midi) {
    // RX is enqueued in tuh_midi2_rx_cb (fast); midi.task() drains the ring
    // and runs the decode off the RX path.
    midi.task();
}

}  // namespace esp32_p4_devkit_host

/*--------------------------------------------------------------------+
 * TinyUSB MIDI 2.0 host callbacks (caller-supplied per midi2_host.h).
 *
 * mount/umount notify the m2host instance of lifecycle changes; m2host
 * then auto-discovers (auto_discover=true by default) by sending UMP
 * Stream Endpoint Discovery + CI Discovery Inquiry. RX is drained
 * inside rx_cb below per packet word count.
 *--------------------------------------------------------------------*/
extern "C" {

void tuh_midi2_descriptor_cb(uint8_t idx,
                              const tuh_midi2_descriptor_cb_t* d) {
    if (idx >= midi2::Host::MAX_DEVICES || !d) return;
    esp32_p4_devkit_host::g_bcdMSC[idx] =
        ((uint16_t)d->bcdMSC_hi << 8) | (uint16_t)d->bcdMSC_lo;
}

void tuh_midi2_mount_cb(uint8_t idx, const tuh_midi2_mount_cb_t* m) {
    if (!esp32_p4_devkit_host::g_midi) return;
    uint16_t bcd = (idx < midi2::Host::MAX_DEVICES)
                     ? esp32_p4_devkit_host::g_bcdMSC[idx] : 0x0200;
    esp32_p4_devkit_host::g_midi->notifyDeviceMounted(
        idx,
        m->protocol_version,
        m->rx_cable_count,
        m->alt_setting_active,
        bcd);
}

void tuh_midi2_rx_cb(uint8_t idx, uint32_t /*xferred_bytes*/) {
    if (!esp32_p4_devkit_host::g_midi) return;
    uint32_t buf[16];
    for (;;) {
        uint32_t n = tuh_midi2_ump_read(idx, buf, 16);
        if (n == 0) break;
        uint32_t i = 0;
        while (i < n) {
            uint8_t mt = (uint8_t)((buf[i] >> 28) & 0x0F);
            uint8_t wc = esp32_p4_devkit_host::kMtWordCount[mt];
            if (i + wc > n) break;
            esp32_p4_devkit_host::g_midi->feedRx(idx, &buf[i], wc);
            i += wc;
        }
    }
}

void tuh_midi2_tx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {
    // No-op for v0.1.
}

void tuh_midi2_umount_cb(uint8_t idx) {
    if (!esp32_p4_devkit_host::g_midi) return;
    esp32_p4_devkit_host::g_midi->notifyDeviceUnmounted(idx);
}

}  // extern "C"

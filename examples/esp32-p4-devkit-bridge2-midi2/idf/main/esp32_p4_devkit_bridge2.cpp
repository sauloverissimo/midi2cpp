/*
 * esp32_p4_devkit_bridge2.cpp, platform glue for the m2bridge-based
 * variant of the ESP32-P4 dual-stack USB MIDI 2.0 bridge.
 *
 * Owns:
 *   - ESP32-P4 USB-OTG dual PHY init: UTMI for host (rhport 1, USB-A
 *     jacks) + INT for device (rhport 0, USB-Device USB-C jack)
 *   - The LP_SYS.usb_ctrl PHY swap on the device side (mandatory on
 *     the Waveshare WIFI6 dev kit, see esp32-p4-devkit-usb-midi2 D-024)
 *   - TinyUSB device + host driver install
 *   - Wiring between TinyUSB and midi2::m2bridge via:
 *       * the bridge's downstream + upstream write functions
 *       * the bridge's slotSetActive / feedHostRx / feedHostMidi1Bytes
 *         from tuh_midi(2)_*_cb callbacks
 *       * device-side RX drained from tud_midi2_n_ump_read into
 *         bridge.feedDeviceRx
 *
 * Compared to the v1 sibling at ../../esp32-p4-devkit-bridge-midi2,
 * this file shrinks substantially: every shared invariant (slot table,
 * group rewrite, multi-FB Stream Discovery responder, ByteStreamConverter
 * wiring) lives inside midi2cpp/src/midi2_bridge.cpp now.
 */
#include "esp32_p4_devkit_bridge2.h"

#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_private/usb_phy.h"
#include "soc/lp_system_struct.h"
#include "tusb.h"
#include "class/midi/midi_host.h"
#include "class/midi/midi2_host.h"

namespace esp32_p4_devkit_bridge2 {

constexpr const char* TAG = "esp32-p4-bridge2";
constexpr uint8_t TUD_RHPORT = 0;
constexpr uint8_t TUH_RHPORT = 1;

// Globals so the C-callable TinyUSB callbacks below can reach the
// shared bridge instance. Set inside init().
midi2::m2bridge* g_bridge = nullptr;
uint16_t        g_bcdMSC[midi2::Bridge::MAX_SLOTS] = {0x0200, 0x0200, 0x0200, 0x0200};

namespace {

size_t platform_dev_write_fn(const uint32_t* words, size_t count) {
    if (!tud_midi2_n_mounted(0)) return 0;
    if (tud_midi2_n_alt_setting(0) != 1) return 0;
    // Whole-message write: a full FIFO mid-burst must not truncate a UMP.
    // Yield to the TinyUSB task and retry (bounded ~100 ms: host gone).
    uint32_t off = 0;
    uint32_t spin = 0;
    while (off < count) {
        off += tud_midi2_n_ump_write(0, words + off, (uint32_t)(count - off));
        if (off >= count) break;
        vTaskDelay(1);
        if (++spin > 100) break;
    }
    return (size_t)off;
}

size_t platform_host_write_fn(uint8_t idx, const uint32_t* words, size_t count) {
    if (!tuh_midi2_mounted(idx)) return 0;
    size_t n = tuh_midi2_ump_write(idx, words, (uint32_t)count);
    tuh_midi2_write_flush(idx);
    return n;
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

// MIDI 1.0 alt 0 slots are allocated from the top of the table so
// they do not collide with MIDI 2.0 mounts (which keep their tuh_midi2
// idx as the bridge slot index). The reverse map per tuh_midi idx
// stores the chosen bridge slot for unmount / RX dispatch.
int8_t g_midi1_slot_map[midi2::Bridge::MAX_SLOTS] = {-1, -1, -1, -1};
bool   g_slot_busy[midi2::Bridge::MAX_SLOTS]      = {false, false, false, false};

uint8_t alloc_slot_from_top() {
    for (int8_t i = midi2::Bridge::MAX_SLOTS - 1; i >= 0; --i) {
        if (!g_slot_busy[(uint8_t)i]) return (uint8_t)i;
    }
    return 0xFF;
}

}  // namespace

void init(midi2::m2bridge& bridge) {
    g_bridge = &bridge;

    bridge.setDownstreamWriteFn(platform_dev_write_fn);
    bridge.setUpstreamWriteFn(platform_host_write_fn);
    bridge.setNowFn(platform_now_fn);
    bridge.setRngFn(platform_rng_fn);

    bridge.begin();

    usb_phy_init_host();
    usb_phy_init_device();

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

void task(midi2::m2bridge& bridge) {
    // Refresh device mount/alt; RX drain on both sides happens in
    // tud_midi2_rx_cb / tuh_midi2_rx_cb below.
    bool mounted = tud_midi2_n_mounted(0);
    bridge.setDeviceMounted(mounted);
    bridge.setDeviceAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);
    bridge.task();
}

}  // namespace esp32_p4_devkit_bridge2

/*--------------------------------------------------------------------+
 * TinyUSB device callbacks (rhport 0).
 *--------------------------------------------------------------------*/
extern "C" {

void tud_midi2_rx_cb(uint8_t itf) {
    if (!esp32_p4_devkit_bridge2::g_bridge) return;
    uint32_t buf[16];
    for (;;) {
        uint32_t n = tud_midi2_n_ump_read(itf, buf, 16);
        if (n == 0) break;
        esp32_p4_devkit_bridge2::g_bridge->feedDeviceRx(buf, n);
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

/*--------------------------------------------------------------------+
 * TinyUSB host callbacks (rhport 1).
 *--------------------------------------------------------------------*/
extern "C" {

void tuh_mount_cb(uint8_t daddr) {
    std::printf("[tuh] generic mount daddr=%u\r\n", daddr);
    std::fflush(stdout);
}

void tuh_umount_cb(uint8_t daddr) {
    std::printf("[tuh] generic umount daddr=%u\r\n", daddr);
    std::fflush(stdout);
}

void tuh_midi2_descriptor_cb(uint8_t idx,
                              const tuh_midi2_descriptor_cb_t* d) {
    std::printf("[tuh-midi2] descriptor idx=%u bcdMSC=%02X.%02X\r\n",
                idx, d ? d->bcdMSC_hi : 0, d ? d->bcdMSC_lo : 0);
    std::fflush(stdout);
    if (idx >= midi2::Bridge::MAX_SLOTS || !d) return;
    esp32_p4_devkit_bridge2::g_bcdMSC[idx] =
        ((uint16_t)d->bcdMSC_hi << 8) | (uint16_t)d->bcdMSC_lo;
}

void tuh_midi2_mount_cb(uint8_t idx, const tuh_midi2_mount_cb_t* m) {
    std::printf("[tuh-midi2] mount idx=%u proto=%u rxCables=%u alt=%u\r\n",
                idx, m ? m->protocol_version : 0,
                m ? m->rx_cable_count : 0,
                m ? m->alt_setting_active : 0);
    std::fflush(stdout);
    if (!esp32_p4_devkit_bridge2::g_bridge || !m) return;
    if (idx >= midi2::Bridge::MAX_SLOTS) return;

    // m2 Host populates DeviceIdentity + fires onDeviceConnected, which
    // the Bridge intercepts to flip slot.active and push FB Info / FB
    // Name. The bridge's own slot table follows the m2 Host idx 1:1
    // for MIDI 2.0 devices.
    uint16_t bcd = esp32_p4_devkit_bridge2::g_bcdMSC[idx];
    esp32_p4_devkit_bridge2::g_bridge->host().notifyDeviceMounted(
        idx, m->protocol_version, m->rx_cable_count,
        m->alt_setting_active, bcd);
    esp32_p4_devkit_bridge2::g_slot_busy[idx] = true;
}

void tuh_midi2_rx_cb(uint8_t idx, uint32_t /*xferred_bytes*/) {
    if (!esp32_p4_devkit_bridge2::g_bridge) return;
    uint32_t buf[16];
    for (;;) {
        uint32_t n = tuh_midi2_ump_read(idx, buf, 16);
        if (n == 0) break;
        esp32_p4_devkit_bridge2::g_bridge->feedHostRx(idx, buf, n);
    }
}
void tuh_midi2_tx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {}

void tuh_midi2_umount_cb(uint8_t idx) {
    std::printf("[tuh-midi2] umount idx=%u\r\n", idx);
    std::fflush(stdout);
    if (!esp32_p4_devkit_bridge2::g_bridge) return;
    if (idx >= midi2::Bridge::MAX_SLOTS) return;
    esp32_p4_devkit_bridge2::g_bridge->host().notifyDeviceUnmounted(idx);
    esp32_p4_devkit_bridge2::g_slot_busy[idx] = false;
}

// Legacy MIDI 1.0 host callbacks (CFG_TUH_MIDI=4). The
// experiment/midi-coexistence branch makes the two host drivers
// disjoint via alt-walk bcdMSC defer, so each driver fires its own
// callback set for the device matching its protocol version. MIDI 1.0
// devices are routed through midi2::ByteStreamConverter inside the
// bridge.
void tuh_midi_descriptor_cb(uint8_t idx, const tuh_midi_descriptor_cb_t* d) {
    (void)d;
    std::printf("[tuh-midi] descriptor idx=%u\r\n", idx);
    std::fflush(stdout);
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t* m) {
    (void)m;
    std::printf("[tuh-midi] mount idx=%u (legacy MIDI 1.0)\r\n", idx);
    std::fflush(stdout);
    if (!esp32_p4_devkit_bridge2::g_bridge) return;
    if (idx >= midi2::Bridge::MAX_SLOTS) return;
    uint8_t slot = esp32_p4_devkit_bridge2::alloc_slot_from_top();
    if (slot == 0xFF) {
        std::printf("[tuh-midi] no free slot for idx=%u\r\n", idx);
        std::fflush(stdout);
        return;
    }
    esp32_p4_devkit_bridge2::g_midi1_slot_map[idx] = (int8_t)slot;
    esp32_p4_devkit_bridge2::g_slot_busy[slot]     = true;
    char name[32];
    std::snprintf(name, sizeof(name), "MIDI 1.0 dev %u", (unsigned)idx);
    esp32_p4_devkit_bridge2::g_bridge->slotSetActive(slot, true, /*alt*/ 0);
    esp32_p4_devkit_bridge2::g_bridge->device().sendFbNameUpdate(slot, name);
}

void tuh_midi_umount_cb(uint8_t idx) {
    std::printf("[tuh-midi] umount idx=%u\r\n", idx);
    std::fflush(stdout);
    if (!esp32_p4_devkit_bridge2::g_bridge) return;
    if (idx >= midi2::Bridge::MAX_SLOTS) return;
    int8_t slot = esp32_p4_devkit_bridge2::g_midi1_slot_map[idx];
    if (slot < 0) return;
    esp32_p4_devkit_bridge2::g_bridge->slotSetActive((uint8_t)slot, false, 0);
    esp32_p4_devkit_bridge2::g_slot_busy[(uint8_t)slot]     = false;
    esp32_p4_devkit_bridge2::g_midi1_slot_map[idx]          = -1;
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t /*xferred_bytes*/) {
    if (!esp32_p4_devkit_bridge2::g_bridge) return;
    if (idx >= midi2::Bridge::MAX_SLOTS) return;
    int8_t slot = esp32_p4_devkit_bridge2::g_midi1_slot_map[idx];
    if (slot < 0) return;
    uint8_t buf[64];
    for (;;) {
        uint32_t n = tuh_midi_packet_read_n(idx, buf, sizeof(buf));
        if (n < 4) break;
        esp32_p4_devkit_bridge2::g_bridge->feedHostMidi1Bytes((uint8_t)slot, buf, n);
    }
}
void tuh_midi_tx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {}

}  // extern "C"

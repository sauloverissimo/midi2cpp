/*
 * esp32_p4_devkit_bridge.cpp, board core implementation (dual-stack).
 *
 * Owns:
 *   - ESP32-P4 USB-OTG dual PHY init: UTMI for host (rhport 1, USB-A
 *     jacks) + INT for device (rhport 0, USB-Device USB-C jack)
 *   - The LP_SYS.usb_ctrl PHY swap on the device side (mandatory on
 *     the Waveshare WIFI6 dev kit, see esp32-p4-devkit-usb-midi2 D-024)
 *   - TinyUSB device + host driver install
 *   - Wiring between TinyUSB and midi2cpp via m2device, m2ci, m2host
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
#include "class/midi/midi_host.h"
#include "class/midi/midi2_host.h"

namespace esp32_p4_devkit_bridge {

constexpr const char* TAG = "esp32-p4-bridge";
constexpr uint8_t TUD_RHPORT = 0;
constexpr uint8_t TUH_RHPORT = 1;

// Globals for C-callable callbacks.
midi2::m2host*    g_host = nullptr;
midi2::m2device*  g_midi_ptr = nullptr;
uint16_t          g_bcdMSC[midi2::Host::MAX_DEVICES] = {0x0200, 0x0200, 0x0200, 0x0200};

// ---- Slot state ----
// One slot per upstream device. Active when an upstream device is
// mounted; carries the upstream's Endpoint Name (when discovered) and
// the alt setting (0 = MIDI 1.0 byte stream forwarded via
// ByteStreamConverter, 1 = UMP forwarded raw with group rewrite).
struct Slot {
    bool    active = false;
    uint8_t alt    = 0;
    char    name[64] = {0};
};
static Slot g_slots[kNumSlots];

// One byte-stream-to-UMP converter per slot. The group is set to the
// slot's first group on activate and the converter emits MT 0x2 UMPs
// into that group. Forwarding to the PC happens inside the onUmp
// callback registered in init().
static midi2::ByteStreamConverter* g_byte_conv[kNumSlots] = {nullptr, nullptr, nullptr, nullptr};

// USB-MIDI 1.0 packet (4 bytes) → number of MIDI bytes to feed the
// converter. Index by CIN (low nibble of byte 0). Reserved CINs 0/1
// yield 0 (skip).
static const uint8_t kCinByteCount[16] = {
    0, 0, 2, 3,   // 0,1,2,3
    3, 1, 2, 3,   // 4,5,6,7
    3, 3, 3, 3,   // 8,9,A,B
    2, 2, 3, 1    // C,D,E,F
};

// UMP word count per Message Type (top nibble of word 0). Used by the
// raw forwarder to step through a stream of words.
static const uint8_t kMtWordCount[16] = {
    1, 1, 1, 2,   // 0,1,2,3
    2, 4, 1, 1,   // 4,5,6,7
    2, 2, 2, 3,   // 8,9,A,B
    3, 4, 4, 4    // C,D,E,F
};

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

// Raw UMP write to the PC-facing endpoint (alt 1, UMP). Drops the
// write when the device side isn't mounted in UMP mode.
uint32_t write_raw_ump_to_pc(const uint32_t* words, uint32_t count) {
    if (!tud_midi2_n_mounted(0) || tud_midi2_n_alt_setting(0) != 1) return 0;
    return tud_midi2_n_ump_write(0, words, count);
}

// Forward a buffer of UMPs from upstream slot `idx` to the PC,
// rewriting each word's group nibble into the slot's window. MT 0x0
// (utility / JR), 0xE (reserved) and 0xF (stream) are not forwarded:
// the bridge owns its own JR heartbeat decision and its own Stream
// Discovery surface; reserved MTs are skipped to keep the wire clean.
void forward_ump_to_pc(uint8_t idx, const uint32_t* words, uint32_t count) {
    if (idx >= kNumSlots) return;
    const uint8_t base = (uint8_t)(idx * kGroupsPerSlot);

    uint32_t i = 0;
    while (i < count) {
        uint8_t mt = (uint8_t)((words[i] >> 28) & 0x0F);
        uint8_t wcount = kMtWordCount[mt];
        if (i + wcount > count) break;

        if (mt != 0x0 && mt != 0xE && mt != 0xF) {
            uint32_t out[4];
            uint8_t in_group = (uint8_t)((words[i] >> 24) & 0x0F);
            uint8_t out_group = (uint8_t)(base + (in_group % kGroupsPerSlot));
            out[0] = (words[i] & 0xF0FFFFFFu)
                   | ((uint32_t)(out_group & 0x0F) << 24);
            for (uint8_t w = 1; w < wcount; ++w) out[w] = words[i + w];
            (void)write_raw_ump_to_pc(out, wcount);
        }
        i += wcount;
    }
}

// Push a Function Block Info Notification for a single slot.
void push_fb_info(uint8_t idx) {
    if (!g_midi_ptr || idx >= kNumSlots) return;
    const uint8_t base = (uint8_t)(idx * kGroupsPerSlot);
    g_midi_ptr->sendFbInfo(/*active*/      g_slots[idx].active,
                           /*fb_num*/      idx,
                           /*direction*/   0x03,            // bidirectional
                           /*ui_hint*/     0x03,            // Sender + Receiver
                           /*first_group*/ base,
                           /*num_groups*/  kGroupsPerSlot,
                           /*midi_ci_ver*/ 0x02,
                           /*sysex8*/      false,
                           /*protocol*/    0x02);
}

// Push a Function Block Name Notification for a single slot. Empty or
// inactive slots fall back to a generic placeholder so the host shows
// the slot count even before any device shows up.
void push_fb_name(uint8_t idx) {
    if (!g_midi_ptr || idx >= kNumSlots) return;
    const char* name = (g_slots[idx].active && g_slots[idx].name[0])
                         ? g_slots[idx].name
                         : "(empty slot)";
    g_midi_ptr->sendFbNameUpdate(idx, name);
}

}  // namespace

void init(midi2::m2device& midi, midi2::m2ci& ci, midi2::m2host& host) {
    g_host = &host;
    g_midi_ptr = &midi;

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

    // Per-slot MIDI 1.0 byte-stream-to-UMP converters. Each one is
    // pinned to its slot's first group; emitted UMPs go straight to
    // the PC via write_raw_ump_to_pc.
    for (uint8_t i = 0; i < kNumSlots; ++i) {
        g_byte_conv[i] = new midi2::ByteStreamConverter((uint8_t)(i * kGroupsPerSlot));
        g_byte_conv[i]->onUmp([](const uint32_t* w, uint8_t cnt) {
            (void)write_raw_ump_to_pc(w, cnt);
        });
    }

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
            // Same per-packet iteration as the host side (see below):
            // m2device's feedRx delegates to midi2_proc_feed which only
            // processes one UMP per call.
            uint32_t i = 0;
            while (i < n) {
                uint8_t mt = (uint8_t)((buf[i] >> 28) & 0x0F);
                uint8_t wc = kMtWordCount[mt];
                if (i + wc > n) break;
                midi.feedRx(&buf[i], wc);
                i += wc;
            }
        }
    }
    midi.task();

    // Host side: drain RX per device idx, forward raw to PC + feed
    // m2host so its Stream Discovery / Identity tracking still fires.
    // m2host's feedRx -> midi2_proc_feed processes one UMP packet per
    // call (ignores word_count and uses MT to size the packet), so we
    // iterate packet-by-packet here. forward_ump_to_pc handles the
    // multi-packet buffer internally.
    for (uint8_t idx = 0; idx < midi2::Host::MAX_DEVICES; ++idx) {
        if (!tuh_midi2_mounted(idx)) continue;
        uint32_t buf[16];
        for (;;) {
            uint32_t n = tuh_midi2_ump_read(idx, buf, 16);
            if (n == 0) break;
            forward_ump_to_pc(idx, buf, n);
            uint32_t i = 0;
            while (i < n) {
                uint8_t mt = (uint8_t)((buf[i] >> 28) & 0x0F);
                uint8_t wc = kMtWordCount[mt];
                if (i + wc > n) break;
                host.feedRx(idx, &buf[i], wc);
                i += wc;
            }
        }
    }
    host.task();
}

// ----------------------------------------------------------------------
// Public slot API. Called from main.cpp on m2host events; also used
// internally by tuh_midi_*_cb to wire legacy MIDI 1.0 devices.
// ----------------------------------------------------------------------

void slot_set_active(uint8_t idx, bool active, uint8_t alt) {
    if (idx >= kNumSlots) return;
    g_slots[idx].active = active;
    g_slots[idx].alt    = alt;
    if (!active) {
        g_slots[idx].name[0] = '\0';
        if (g_byte_conv[idx]) g_byte_conv[idx]->reset();
    }
    std::printf("[slot] idx=%u active=%d alt=%u\r\n",
                (unsigned)idx, (int)active, (unsigned)alt);
    std::fflush(stdout);
    push_fb_info(idx);
    push_fb_name(idx);
}

void slot_set_name(uint8_t idx, const char* name) {
    if (idx >= kNumSlots || !name) return;
    std::strncpy(g_slots[idx].name, name, sizeof(g_slots[idx].name) - 1);
    g_slots[idx].name[sizeof(g_slots[idx].name) - 1] = '\0';
    std::printf("[slot] idx=%u name='%s'\r\n", (unsigned)idx, g_slots[idx].name);
    std::fflush(stdout);
    push_fb_name(idx);
}

void push_slot_advertisement(uint8_t idx, uint8_t filter) {
    if (idx >= kNumSlots) return;
    if (filter & 0x01) push_fb_info(idx);
    if (filter & 0x02) push_fb_name(idx);
}

// Allocate the highest-numbered free slot. Used for MIDI 1.0 legacy
// devices so they don't collide with MIDI 2.0 devices coming in
// through m2host (which take low-numbered slots).
uint8_t alloc_slot_from_top() {
    for (int8_t i = kNumSlots - 1; i >= 0; --i) {
        if (!g_slots[i].active) return (uint8_t)i;
    }
    return 0xFF;
}
// Reverse map for MIDI 1.0: tuh_midi idx → bridge slot. -1 if none.
int8_t g_midi1_slot_map[kNumSlots] = {-1, -1, -1, -1};

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
    if (idx >= midi2::Host::MAX_DEVICES || !d) return;
    esp32_p4_devkit_bridge::g_bcdMSC[idx] =
        ((uint16_t)d->bcdMSC_hi << 8) | (uint16_t)d->bcdMSC_lo;
}

void tuh_midi2_mount_cb(uint8_t idx, const tuh_midi2_mount_cb_t* m) {
    std::printf("[tuh-midi2] mount idx=%u proto=%u rxCables=%u alt=%u\r\n",
                idx, m ? m->protocol_version : 0,
                m ? m->rx_cable_count : 0,
                m ? m->alt_setting_active : 0);
    std::fflush(stdout);
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
    std::printf("[tuh-midi2] umount idx=%u\r\n", idx);
    std::fflush(stdout);
    if (!esp32_p4_devkit_bridge::g_host) return;
    esp32_p4_devkit_bridge::g_host->notifyDeviceUnmounted(idx);
}

// Legacy MIDI 1.0 host callbacks (CFG_TUH_MIDI=1). With the experimental
// alt-walk defer, these only fire for devices whose MIDIStreaming
// interface has no alt setting advertising bcdMSC>=0x0200 (i.e. legacy
// MIDI 1.0 controllers like the Arturia MiniLab). Bridge allocates a
// slot from the top, names it "MIDI1.0 [idx]" by default, and feeds
// USB-MIDI 1.0 packets through midi2::ByteStreamConverter so the PC
// receives MT 0x2 UMPs in the slot's first group.
void tuh_midi_descriptor_cb(uint8_t idx, const tuh_midi_descriptor_cb_t* d) {
    (void)d;
    std::printf("[tuh-midi] descriptor idx=%u\r\n", idx);
    std::fflush(stdout);
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t* m) {
    (void)m;
    if (idx >= esp32_p4_devkit_bridge::kNumSlots) {
        std::printf("[tuh-midi] mount idx=%u (legacy, idx out of range)\r\n", idx);
        std::fflush(stdout);
        return;
    }
    uint8_t slot = esp32_p4_devkit_bridge::alloc_slot_from_top();
    if (slot == 0xFF) {
        std::printf("[tuh-midi] mount idx=%u (legacy, no free slot)\r\n", idx);
        std::fflush(stdout);
        return;
    }
    esp32_p4_devkit_bridge::g_midi1_slot_map[idx] = (int8_t)slot;
    std::printf("[tuh-midi] mount idx=%u -> bridge slot %u (legacy MIDI 1.0)\r\n",
                idx, (unsigned)slot);
    std::fflush(stdout);

    char fallback[32];
    std::snprintf(fallback, sizeof(fallback), "MIDI 1.0 dev %u", (unsigned)idx);
    esp32_p4_devkit_bridge::slot_set_active(slot, /*active*/ true, /*alt*/ 0);
    esp32_p4_devkit_bridge::slot_set_name(slot, fallback);
}

void tuh_midi_umount_cb(uint8_t idx) {
    std::printf("[tuh-midi] umount idx=%u\r\n", idx);
    std::fflush(stdout);
    if (idx >= esp32_p4_devkit_bridge::kNumSlots) return;
    int8_t slot = esp32_p4_devkit_bridge::g_midi1_slot_map[idx];
    if (slot < 0) return;
    esp32_p4_devkit_bridge::slot_set_active((uint8_t)slot, /*active*/ false, /*alt*/ 0);
    esp32_p4_devkit_bridge::g_midi1_slot_map[idx] = -1;
}

// Drains the USB-MIDI 1.0 RX FIFO and feeds each packet's MIDI bytes
// (count derived from the CIN) into the slot's ByteStreamConverter.
// CIN 0x0/0x1 are reserved/no-op; everything else carries 1..3 MIDI
// bytes per 4-byte USB-MIDI 1.0 packet (USB-MIDI 1.0 §4).
void tuh_midi_rx_cb(uint8_t idx, uint32_t /*xferred_bytes*/) {
    if (idx >= esp32_p4_devkit_bridge::kNumSlots) return;
    int8_t slot = esp32_p4_devkit_bridge::g_midi1_slot_map[idx];
    if (slot < 0) return;
    auto* conv = esp32_p4_devkit_bridge::g_byte_conv[slot];
    if (!conv) return;

    uint8_t buf[64];
    for (;;) {
        uint32_t n = tuh_midi_packet_read_n(idx, buf, sizeof(buf));
        if (n < 4) break;
        for (uint32_t off = 0; off + 4 <= n; off += 4) {
            uint8_t cin = buf[off] & 0x0F;
            uint8_t bcount = esp32_p4_devkit_bridge::kCinByteCount[cin];
            for (uint8_t b = 0; b < bcount; ++b) {
                (void)conv->feed(buf[off + 1 + b]);
            }
        }
    }
}
void tuh_midi_tx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {}

}  // extern "C"

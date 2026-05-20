/*
 * main.cpp, esp32-p4-devkit-bridge-midi2 (multi-slot)
 *
 * Dual-stack USB MIDI 2.0 bridge on the Waveshare ESP32-P4-WIFI6-DEV-KIT.
 *
 *   USB-A jacks (UTMI host, rhport 1)
 *     up to 4 upstream MIDI 2.0 devices (m2host slots 0..3) and/or
 *     legacy MIDI 1.0 devices (USB-MIDI 1.0 alt 0). Each upstream
 *     device occupies a 4-group window on the PC side and owns one
 *     Function Block whose name reflects the upstream Endpoint Name.
 *
 *   USB-Device USB-C (INT device, rhport 0)
 *     PC sees ESP32P4Bridge (cafe:4092) with 16 groups partitioned
 *     into 4 FBs:
 *       FB 0 -> groups 0..3   (slot 0)
 *       FB 1 -> groups 4..7   (slot 1)
 *       FB 2 -> groups 8..11  (slot 2)
 *       FB 3 -> groups 12..15 (slot 3)
 *
 * Forwarding is at the UMP word level (in esp32_p4_devkit_bridge.cpp):
 * each upstream UMP keeps its MT, status and payload but its group
 * nibble is rewritten into the slot's window. MIDI 1.0 alt-0 devices
 * are bridged via midi2::ByteStreamConverter so they show up as MT 0x2
 * UMPs on the slot's first group.
 *
 * This file is the orchestration layer: it installs the UMP Stream
 * Discovery responder (so the PC sees the dynamic 4-FB topology with
 * per-device names) and forwards m2host lifecycle events into the
 * bridge slot table.
 */
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_p4_devkit_bridge.h"

using namespace midi2;

// Globals: constructor at C++ init time, no static-init race in app_main.
static m2device g_midi;
static m2ci     g_ci(g_midi);
static m2host   g_host;

// Educational/non-commercial MIDI-CI manufacturer prefix
// (MIDI Association block).
static constexpr uint8_t  kManufacturerId[3] = {0x7D, 0x00, 0x00};
static constexpr uint16_t kFamily            = 0x0001;
static constexpr uint16_t kModel             = 0x0001;
static constexpr uint32_t kVersion           = 0x00010000;
static constexpr const char* kEndpointName     = "ESP32P4Bridge";
static constexpr const char* kProductInstance = "ESP32P4Bridge-0001";

extern "C" void app_main(void) {
    vTaskDelay(1);
    std::printf("\r\n[boot] esp32-p4-devkit-bridge-midi2 (multi-slot)\r\n");
    std::fflush(stdout);

    g_midi.begin();
    g_ci.begin(kManufacturerId, kFamily, kModel, kVersion);

    // ------------------------------------------------------------------
    // UMP Stream Discovery responder (PC-facing). The
    // experiment/midi-coexistence opt-in user responder
    // (CFG_TUD_MIDI2_USER_RESPONDER=1) lets MT 0xF Stream messages
    // pass through to the app so we can answer with per-FB group
    // windows + dynamic FB Names tied to each slot.
    // ------------------------------------------------------------------
    g_midi.onEndpointDiscovery([](uint8_t filter) {
        if (filter & 0x01) {
            g_midi.sendEndpointInfo(/*ump_ver*/ 1, 1,
                                    /*static_fb*/ false,
                                    /*num_fb*/ esp32_p4_devkit_bridge::kNumSlots,
                                    /*midi2*/ true, /*midi1*/ true,
                                    /*rx_jr*/ false, /*tx_jr*/ true);
        }
        if (filter & 0x02) g_midi.sendDeviceIdentity(kManufacturerId, kFamily, kModel, kVersion);
        if (filter & 0x04) g_midi.sendEndpointNameUpdate(kEndpointName);
        if (filter & 0x08) g_midi.sendProductInstanceIdUpdate(kProductInstance);
        if (filter & 0x10) g_midi.sendStreamConfigNotify(/*protocol*/ 0x02);
    });

    g_midi.onFbDiscovery([](uint8_t fbNum, uint8_t filter) {
        if (fbNum == 0xFF) {
            for (uint8_t i = 0; i < esp32_p4_devkit_bridge::kNumSlots; ++i) {
                esp32_p4_devkit_bridge::push_slot_advertisement(i, filter);
            }
        } else {
            esp32_p4_devkit_bridge::push_slot_advertisement(fbNum, filter);
        }
    });

    g_midi.onStreamConfigRequest([](uint8_t protocol) {
        g_midi.sendStreamConfigNotify(protocol);
    });

    // ------------------------------------------------------------------
    // Host-side lifecycle: each upstream MIDI 2.0 device's m2host idx
    // is used directly as a bridge slot index (m2host MAX_DEVICES ==
    // kNumSlots). Legacy MIDI 1.0 devices are wired in
    // esp32_p4_devkit_bridge.cpp's tuh_midi_*_cb directly.
    // ------------------------------------------------------------------
    g_host.onDeviceConnected([](uint8_t idx, const m2host::DeviceIdentity& id) {
        std::printf("[host] device idx=%u connected, alt=%u\r\n",
                    idx, id.altSettingActive);
        std::fflush(stdout);
        esp32_p4_devkit_bridge::slot_set_active(idx, /*active*/ true,
                                                id.altSettingActive);
    });
    g_host.onDeviceDisconnected([](uint8_t idx) {
        std::printf("[host] device idx=%u disconnected\r\n", idx);
        std::fflush(stdout);
        esp32_p4_devkit_bridge::slot_set_active(idx, /*active*/ false, /*alt*/ 0);
    });
    g_host.onIdentityUpdated([](uint8_t idx, const m2host::DeviceIdentity& id) {
        std::printf("[ep] idx=%u UMP v%u.%u FB=%u M2=%d EPname='%s'(%u) ProdID='%s'(%u)\r\n",
                    idx, id.umpVerMajor, id.umpVerMinor,
                    id.numFunctionBlocks, id.supportsMidi2Protocol,
                    id.endpointName, (unsigned)strlen(id.endpointName),
                    id.productInstanceId, (unsigned)strlen(id.productInstanceId));
        std::fflush(stdout);
        if (id.endpointName[0]) {
            esp32_p4_devkit_bridge::slot_set_name(idx, id.endpointName);
        }
    });

    std::printf("[boot] callbacks installed, starting bridge...\r\n");
    std::fflush(stdout);

    esp32_p4_devkit_bridge::init(g_midi, g_ci, g_host);

    std::printf("[bridge] PC sees %s (cafe:4092), %u groups across %u FBs.\r\n",
                kEndpointName,
                (unsigned)(esp32_p4_devkit_bridge::kNumSlots * esp32_p4_devkit_bridge::kGroupsPerSlot),
                (unsigned)esp32_p4_devkit_bridge::kNumSlots);
    std::printf("[bridge] Plug MIDI 2.0 or MIDI 1.0 devices into a USB-A jack.\r\n");
    std::fflush(stdout);

    while (true) {
        esp32_p4_devkit_bridge::task(g_midi, g_host);
        vTaskDelay(1);
    }
}

/*
 * main.cpp, esp32-p4-devkit-bridge2-midi2
 *
 * Multi-slot USB MIDI 2.0 bridge built on the reusable midi2::m2bridge
 * class. The application here is just identity setup + the main loop;
 * everything that used to live inline in main.cpp / esp32_p4_devkit_bridge.cpp
 * (slot table, group rewrite, FB Name dispatch, MIDI 1.0 byte uplift,
 * Stream Discovery responder) now lives in midi2cpp/src/midi2_bridge.cpp.
 *
 *   USB-A jacks (UTMI host, rhport 1)
 *     up to MAX_SLOTS upstream MIDI 2.0 devices, plus legacy MIDI 1.0
 *     devices via the alt-walk bcdMSC defer in TinyUSB. Each upstream
 *     device occupies a 4-group window on the PC side and owns one
 *     Function Block whose name reflects the upstream Endpoint Name.
 *
 *   USB-Device USB-C (INT device, rhport 0)
 *     PC sees ESP32P4Bridge2 (cafe:4095) with 16 groups partitioned
 *     into 4 FBs:
 *       FB 0 -> groups 0..3   (slot 0)
 *       FB 1 -> groups 4..7   (slot 1)
 *       FB 2 -> groups 8..11  (slot 2)
 *       FB 3 -> groups 12..15 (slot 3)
 */
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_p4_devkit_bridge2.h"

using namespace midi2;

static m2bridge g_bridge;

// MIDI Association educational/non-commercial prefix.
static constexpr uint8_t  kManufacturerId[3] = {0x7D, 0x00, 0x00};
static constexpr uint16_t kFamily            = 0x0001;
static constexpr uint16_t kModel             = 0x0001;
static constexpr uint32_t kVersion           = 0x00010000;
static constexpr const char* kEndpointName     = "ESP32P4Bridge2";
static constexpr const char* kProductInstance = "ESP32P4Bridge2-0001";

extern "C" void app_main(void) {
    vTaskDelay(1);
    std::printf("\r\n[boot] esp32-p4-devkit-bridge2-midi2 (m2bridge)\r\n");
    std::fflush(stdout);

    g_bridge.setManufacturerId(kManufacturerId);
    g_bridge.setFamily(kFamily);
    g_bridge.setModel(kModel);
    g_bridge.setVersion(kVersion);
    g_bridge.setEndpointName(kEndpointName);
    g_bridge.setProductInstanceId(kProductInstance);

    esp32_p4_devkit_bridge2::init(g_bridge);

    std::printf("[bridge] PC sees %s (cafe:4095), %u groups across %u FBs.\r\n",
                kEndpointName,
                (unsigned)(g_bridge.numSlots() * g_bridge.groupsPerSlot()),
                (unsigned)g_bridge.numSlots());
    std::printf("[bridge] Plug MIDI 2.0 or MIDI 1.0 devices into a USB-A jack.\r\n");
    std::fflush(stdout);

    while (true) {
        esp32_p4_devkit_bridge2::task(g_bridge);
        vTaskDelay(1);
    }
}

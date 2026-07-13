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
static constexpr uint16_t kModel             = 0x0014;   // fleet-unique (devices use 0x0001..0x0012)
static constexpr uint32_t kVersion           = 0x00010000;
static constexpr const char* kEndpointName     = "ESP32-P4 Bridge2 MIDI 2.0";
static constexpr const char* kProductInstance = "ESP32P4Bridge2-0001";

// MIDI-CI category backing for the composed CI (m2bridge defaults to
// ciCat 0x1C = Profile | Property Exchange | Process Inquiry); every
// advertised category answers.
static const uint8_t kProfileId[5] = {0x7E, 0x00, 0x00, 0x01, 0x00};   // GM 1
static const char kDeviceInfo[] =
    "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[20,0],"
     "\"versionId\":[0,0,4,0],\"manufacturer\":\"midi2.diy\","
     "\"family\":\"Bridge\",\"model\":\"ESP32-P4 Bridge2 MIDI 2.0\","
     "\"version\":\"0.0.1\"}";
static const char kChannelList[] = "[{\"title\":\"Bridge\",\"channel\":1}]";
static const char kProgramList[] = "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]";

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

    g_bridge.ci().addProfile(kProfileId, /*alwaysOn*/ false);
    g_bridge.ci().addPropertyStatic("DeviceInfo",  kDeviceInfo);
    g_bridge.ci().addPropertyStatic("ChannelList", kChannelList);
    g_bridge.ci().addPropertyStatic("ProgramList", kProgramList);
    g_bridge.ci().setMidiReport(/*msg_data_control*/ 0x01,
                                /*system bitmap*/    0x00000000FFFFFFFFull,
                                /*channel bitmap*/   0xFFFFFFFFFFFFFFFFull,
                                /*note bitmap*/      0xFFFFFFFFFFFFFFFFull);

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

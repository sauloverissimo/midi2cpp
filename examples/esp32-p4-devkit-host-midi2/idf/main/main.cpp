/*
 * main.cpp, esp32-p4-devkit-host-midi2-monitor
 *
 * USB MIDI 2.0 host on the Waveshare ESP32-P4-WIFI6-DEV-KIT. Receives
 * UMP from any MIDI 2.0 device plugged into the two USB-A jacks (UTMI
 * PHY in HOST mode, rhport 1), decodes it via m2host's typed
 * callbacks, and prints the device topology + live UMP stream on the
 * UART console (CH343 USB-to-UART bridge on the "ToUART" USB-C jack).
 *
 * Pair this host with any midi2cpp device recipe (rp2040-midi2,
 * waveshare-rp2040-midi2, esp32-s3-devkitc-usb-midi2,
 * esp32-p4-devkit-usb-midi2, etc.) plugged into a USB-A jack to see
 * the full Showcase A-J cycle decoded live on the UART console.
 */
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_p4_devkit_host.h"

using namespace midi2;

// Global m2host: constructor at C++ init time, no static-init race
// inside app_main scope.
static m2host g_host;

extern "C" void app_main(void) {
    vTaskDelay(1);
    std::printf("\r\n[boot] esp32-p4-devkit-host-midi2-monitor\r\n");
    std::fflush(stdout);

    g_host.onDeviceConnected([](uint8_t idx, const m2host::DeviceIdentity& id) {
        std::printf("[host] device idx=%u connected, alt=%u (%s)\r\n",
                    idx, id.altSettingActive,
                    id.altSettingActive == 1 ? "UMP" : "byte-stream");
    });
    g_host.onDeviceDisconnected([](uint8_t idx) {
        std::printf("[host] device idx=%u disconnected\r\n", idx);
    });
    g_host.onIdentityUpdated([](uint8_t idx, const m2host::DeviceIdentity& id) {
        std::printf("[ep] idx=%u UMP v%u.%u, %u FB, MIDI2=%d\r\n",
                    idx, id.umpVerMajor, id.umpVerMinor,
                    id.numFunctionBlocks, id.supportsMidi2Protocol);
        if (id.endpointName[0]) {
            std::printf("[ep] idx=%u Endpoint Name: %s\r\n", idx, id.endpointName);
        }
    });
    g_host.onNoteOn([](uint8_t idx, uint8_t channel, uint8_t note, uint16_t vel) {
        std::printf("[in idx%u] NoteOn ch=%u note=%u vel=0x%04X\r\n",
                    idx, channel, note, vel);
    });
    g_host.onNoteOff([](uint8_t idx, uint8_t channel, uint8_t note, uint16_t vel) {
        std::printf("[in idx%u] NoteOff ch=%u note=%u vel=0x%04X\r\n",
                    idx, channel, note, vel);
    });
    g_host.onCC([](uint8_t idx, uint8_t channel, uint8_t cc_idx, uint32_t val) {
        std::printf("[in idx%u] CC ch=%u #%u val=0x%08X\r\n",
                    idx, channel, cc_idx, (unsigned)val);
    });
    g_host.onPitchBend([](uint8_t idx, uint8_t channel, uint32_t val) {
        std::printf("[in idx%u] PitchBend ch=%u val=0x%08X\r\n",
                    idx, channel, (unsigned)val);
    });

    std::printf("[boot] callbacks installed, starting host...\r\n");
    std::fflush(stdout);

    esp32_p4_devkit_host::init(g_host);

    std::printf("[host] waiting for device on USB-A jacks...\r\n");
    std::fflush(stdout);

    while (true) {
        esp32_p4_devkit_host::task(g_host);
        vTaskDelay(1);
    }
}

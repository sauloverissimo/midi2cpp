/*
 * main.cpp, esp32-p4-devkit-bridge-midi2
 *
 * Dual-stack USB MIDI 2.0 bridge on the Waveshare ESP32-P4-WIFI6-DEV-KIT.
 *
 *   USB-A (UTMI host, rhport 1)  --[m2host]--+
 *                                            |
 *   USB-Device USB-C (INT device, rhport 0) <+-- forwards UMP from any
 *                                                upstream device into
 *                                                the host PC's view of
 *                                                ESP32P4Bridge (PID 0x4092)
 *
 * The bridge instantiates m2device + m2ci (PC-facing endpoint identity and
 * MIDI-CI responder) plus m2host (upstream device discovery + RX dispatch).
 * Forwarding is done at the typed-callback layer: when a host-side device
 * sends NoteOn/NoteOff/CC/PitchBend/Pressure/PolyPressure/PerNote*, the
 * bridge re-emits the same on the device side. Stream Discovery and
 * MIDI-CI are NOT forwarded; each side answers locally so neither stack
 * loops back on the other.
 *
 * Pair this bridge with any midi2_cpp device recipe (rp2040-midi2,
 * waveshare-rp2040-midi2, esp32-s3-devkitc-usb-midi2,
 * esp32-p4-devkit-usb-midi2, etc.) plugged into a USB-A jack and watch
 * the host PC enumerate ESP32P4Bridge as a MIDI 2.0 endpoint that
 * forwards everything the upstream device emits.
 */
#include <cstdio>

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
static constexpr uint8_t kManufacturerId[3] = {0x7D, 0x00, 0x00};
static constexpr uint16_t kFamily  = 0x0001;
static constexpr uint16_t kModel   = 0x0001;
static constexpr uint32_t kVersion = 0x00010000;

extern "C" void app_main(void) {
    vTaskDelay(1);
    std::printf("\r\n[boot] esp32-p4-devkit-bridge-midi2\r\n");
    std::fflush(stdout);

    // ---- Device side: identity + showcase senders (PC-facing) ----
    g_midi.begin();
    g_ci.begin(kManufacturerId, kFamily, kModel, kVersion);
    g_midi.enableJRHeartbeat(500);

    // ---- Host side: monitor + identity tracking ----
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

    // ---- Forwarding: upstream device (USB-A) -> host PC (USB-Device) ----
    // Typed callbacks fire on host RX; we re-emit through m2device so the
    // PC sees a coherent UMP stream from ESP32P4Bridge. Group is squashed
    // to 0 because the device side advertises a single Function Block.
    g_host.onNoteOn([](uint8_t idx, uint8_t channel, uint8_t note, uint16_t vel) {
        std::printf("[fwd idx%u] NoteOn ch=%u note=%u vel=0x%04X\r\n",
                    idx, channel, note, vel);
        g_midi.noteOn(channel, note, vel);
    });
    g_host.onNoteOff([](uint8_t idx, uint8_t channel, uint8_t note, uint16_t vel) {
        std::printf("[fwd idx%u] NoteOff ch=%u note=%u vel=0x%04X\r\n",
                    idx, channel, note, vel);
        g_midi.noteOff(channel, note, vel);
    });
    g_host.onCC([](uint8_t idx, uint8_t channel, uint8_t cc_idx, uint32_t val) {
        std::printf("[fwd idx%u] CC ch=%u #%u val=0x%08X\r\n",
                    idx, channel, cc_idx, (unsigned)val);
        g_midi.cc(channel, cc_idx, val);
    });
    g_host.onPitchBend([](uint8_t idx, uint8_t channel, uint32_t val) {
        std::printf("[fwd idx%u] PitchBend ch=%u val=0x%08X\r\n",
                    idx, channel, (unsigned)val);
        g_midi.pitchBend(channel, val);
    });
    g_host.onChannelPressure([](uint8_t idx, uint8_t channel, uint32_t val) {
        g_midi.sendChannelPressure(0, channel, val);
    });
    g_host.onPolyPressure([](uint8_t idx, uint8_t channel, uint8_t note, uint32_t val) {
        g_midi.sendPolyPressure(0, channel, note, val);
    });
    g_host.onPerNotePitchBend([](uint8_t idx, uint8_t group, uint8_t channel,
                                 uint8_t note, uint32_t val) {
        (void)group;
        g_midi.sendPerNotePitchBend(0, channel, note, val);
    });
    g_host.onProgram([](uint8_t idx, uint8_t group, uint8_t channel, uint8_t program,
                       uint8_t bankMSB, uint8_t bankLSB, bool bankValid) {
        (void)idx; (void)group;
        g_midi.sendProgram(0, channel, program, bankMSB, bankLSB, bankValid);
    });

    std::printf("[boot] dual-stack callbacks installed, starting bridge...\r\n");
    std::fflush(stdout);

    esp32_p4_devkit_bridge::init(g_midi, g_ci, g_host);

    std::printf("[bridge] PC sees ESP32P4Bridge (cafe:4092). Plug a MIDI 2.0\r\n");
    std::printf("[bridge] device into a USB-A jack and the bridge will\r\n");
    std::printf("[bridge] forward NoteOn/Off/CC/PB/Pressure to the PC.\r\n");
    std::fflush(stdout);

    while (true) {
        esp32_p4_devkit_bridge::task(g_midi, g_host);
        vTaskDelay(1);
    }
}

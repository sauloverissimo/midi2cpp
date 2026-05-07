// midi2_cpp | esp32-s3-devkitc-host-midi2
//
// Showcase of the released-grade USB MIDI 2.0 Host path on ESP32-S3:
//
//   USB cable
//      |
//      v
//   ESP32-S3 USB-OTG (DWC2 FS, ESP-IDF native usb_host_*)
//      |
//      v
//   ESP32_Host_MIDI v6.0.0 USBMIDI2Connection
//      * alt-walk: prefers Alt 1 (bcdMSC = 0x0200, MIDI 2.0)
//      * Endpoint Discovery + Endpoint Info + Endpoint Name + Prod ID
//      * Function Block Discovery (up to 8 FBs)
//      * Group Terminal Block descriptor read
//      * Stream Config Request (negotiates MIDI 2.0 protocol)
//      * Explicit SET_INTERFACE on EP0 after Alt 1 claim (v6.0 fix)
//      * UMP RX via setUMPCallback (32-bit words, raw)
//      * UMP TX via sendUMPMessage
//      |
//      v   FreeRTOS queue (cross-core handover, USB task -> main loop)
//      |
//      v
//   midi2_cpp m2host
//      * feedRx + task in single-threaded main-loop context
//      * typed dispatch (NoteOn/Off, CC, PB, ChnPres, Program, ...)
//      * Endpoint Discovery / Identity tracking per device
//      * MIDI-CI Initiator (host MUID, Discovery Inquiry, Reply tracking)
//      |
//      v
//   User callbacks (this file): printf to UART0 (CP2102 left jack)
//
// No TinyUSB. No fork. No PR-pending override. Released libraries only.
//
// v6.0 note: ESP32_Host_MIDI no longer auto-includes USBConnection or
// BLEConnection from the umbrella header. Only the transport headers we
// actually use are pulled in below; the legacy ESP32_HOST_MIDI_NO_USB_HOST
// build flag is therefore obsolete and intentionally absent.

#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <ESP32_Host_MIDI.h>
#include <USBMIDI2Connection.h>

#include "midi2_host.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

USBMIDI2Connection usbMIDI2;
midi2::m2host      host;

// Cross-core UMP handover. USBMIDI2Connection's UMP callback fires from the
// dedicated USB host FreeRTOS task on core 0; m2host expects feedRx from a
// single owning task (main loop, core 1). This queue serializes the words.
struct UMPBatch {
    uint32_t words[8];
    uint8_t  count;
};
static QueueHandle_t s_umpQueue = nullptr;

// Mount-transition detection.
static bool s_wasNegotiated = false;
static uint32_t s_lastRetryMs = 0;
static uint8_t  s_retryCount = 0;

// ---------------------------------------------------------------------------
// USB host -> queue (runs on core 0, USB task)
// ---------------------------------------------------------------------------

static void onUMPFromUsb(void* ctx, const uint32_t* words, uint8_t count) {
    QueueHandle_t q = (QueueHandle_t)ctx;
    UMPBatch batch;
    uint8_t n = (count > 8) ? 8 : count;
    for (uint8_t i = 0; i < n; ++i) batch.words[i] = words[i];
    batch.count = n;
    xQueueSend(q, &batch, 0);
}

// ---------------------------------------------------------------------------
// m2host -> USB host (runs on core 1, main loop)
// ---------------------------------------------------------------------------

static void onUMPToUsb(uint8_t /*idx*/, const uint32_t* words, size_t count) {
    usbMIDI2.sendUMPMessage(words, (uint8_t)count);
}

// ---------------------------------------------------------------------------
// Identity printer
// ---------------------------------------------------------------------------

static void printIdentity(uint8_t idx, const midi2::Host::DeviceIdentity& id) {
    Serial.printf("[Identity ] dev=%u alt=%u bcdMSC=0x%04X UMP=%u.%u FBs=%u\n",
                  idx, id.altSettingActive, id.bcdMSC,
                  id.umpVerMajor, id.umpVerMinor, id.numFunctionBlocks);
    Serial.printf("[Identity ]   protocols  m1=%s m2=%s\n",
                  id.supportsMidi1Protocol ? "yes" : "no",
                  id.supportsMidi2Protocol ? "yes" : "no");
    if (id.endpointName[0])
        Serial.printf("[Identity ]   ep name    \"%s\"\n", id.endpointName);
    if (id.productInstanceId[0])
        Serial.printf("[Identity ]   prod id    \"%s\"\n", id.productInstanceId);
    Serial.printf("[Identity ]   manuf      %02X %02X %02X  family=0x%04X model=0x%04X ver=0x%08X\n",
                  id.manufacturerId[0], id.manufacturerId[1], id.manufacturerId[2],
                  id.familyId, id.modelId, id.version);
    if (id.ciDiscovered)
        Serial.printf("[Identity ]   ci muid    0x%07X (remote)\n", id.ciMuid);
    else if (id.ciDiscoveryPending)
        Serial.printf("[Identity ]   ci muid    pending (request id 0x%08X)\n",
                      id.ciDiscoveryRequestId);
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=====================================================");
    Serial.println("  midi2_cpp  |  ESP32-S3-DevKitC-1  |  USB MIDI 2.0 Host");
    Serial.println("=====================================================");
    Serial.println("  ESP32_Host_MIDI v6.0.0 (handler optional, no fork)");
    Serial.println("  midi2_cpp m2host (typed dispatch + CI Initiator)");
    Serial.println("  Plug a USB MIDI 2.0 device into the OTG jack (right).");
    Serial.println();

    s_umpQueue = xQueueCreate(32, sizeof(UMPBatch));

    // ---- midi2_cpp host wiring ----
    host.setWriteFn(onUMPToUsb);
    host.setNowFn([]() -> uint32_t { return (uint32_t)millis(); });
    host.setRngFn([]() -> uint32_t { return esp_random(); });

    host.onDeviceConnected([](uint8_t idx,
                              const midi2::Host::DeviceIdentity& id) {
        Serial.printf("\n[Connected] dev=%u\n", idx);
        printIdentity(idx, id);
    });
    host.onDeviceDisconnected([](uint8_t idx) {
        Serial.printf("\n[Disconnected] dev=%u\n\n", idx);
    });
    host.onIdentityUpdated([](uint8_t idx,
                              const midi2::Host::DeviceIdentity& id) {
        printIdentity(idx, id);
    });

    host.onNoteOn([](uint8_t idx, uint8_t group, uint8_t channel,
                     uint8_t note, uint16_t velocity,
                     uint8_t /*attrType*/, uint16_t /*attrData*/) {
        Serial.printf("[NoteOn  ] dev=%u g=%u ch=%-2u note=%-3u vel=0x%04X\n",
                      idx, group, channel + 1, note, velocity);
    });
    host.onNoteOff([](uint8_t idx, uint8_t group, uint8_t channel,
                      uint8_t note, uint16_t velocity,
                      uint8_t /*attrType*/, uint16_t /*attrData*/) {
        Serial.printf("[NoteOff ] dev=%u g=%u ch=%-2u note=%-3u vel=0x%04X\n",
                      idx, group, channel + 1, note, velocity);
    });
    host.onCC([](uint8_t idx, uint8_t group, uint8_t channel,
                 uint8_t index, uint32_t value) {
        Serial.printf("[CC #%-3u ] dev=%u g=%u ch=%-2u val=0x%08X\n",
                      index, idx, group, channel + 1, value);
    });
    host.onPitchBend([](uint8_t idx, uint8_t group, uint8_t channel,
                        uint32_t value) {
        int32_t signedPb = (int32_t)((int64_t)value - (int64_t)0x80000000UL);
        Serial.printf("[PitchBnd] dev=%u g=%u ch=%-2u val=%+ld\n",
                      idx, group, channel + 1, (long)signedPb);
    });
    host.onChannelPressure([](uint8_t idx, uint8_t group, uint8_t channel,
                              uint32_t value) {
        Serial.printf("[ChnPres ] dev=%u g=%u ch=%-2u val=0x%08X\n",
                      idx, group, channel + 1, value);
    });
    host.onPolyPressure([](uint8_t idx, uint8_t group, uint8_t channel,
                           uint8_t note, uint32_t value) {
        Serial.printf("[PolyPres] dev=%u g=%u ch=%-2u note=%-3u val=0x%08X\n",
                      idx, group, channel + 1, note, value);
    });
    host.onProgram([](uint8_t idx, uint8_t group, uint8_t channel,
                      uint8_t program, uint8_t bMSB, uint8_t bLSB,
                      bool bankValid) {
        if (bankValid)
            Serial.printf("[Program ] dev=%u g=%u ch=%-2u prog=%-3u bank=%u/%u\n",
                          idx, group, channel + 1, program, bMSB, bLSB);
        else
            Serial.printf("[Program ] dev=%u g=%u ch=%-2u prog=%-3u\n",
                          idx, group, channel + 1, program);
    });
    host.onTempo([](uint8_t idx, uint8_t group, uint32_t tenNsPerQn) {
        // tenNsPerQn = 10 ns per quarter note (UMP Flex Tempo).
        // BPM = 60e9 / tenNsPerQn. Use uint64 to avoid overflow.
        uint64_t bpm_x100 = (uint64_t)6000000000ULL / (uint64_t)tenNsPerQn;
        Serial.printf("[Tempo   ] dev=%u g=%u  %llu.%02llu BPM (10ns/qn=%u)\n",
                      idx, group,
                      (unsigned long long)(bpm_x100 / 100),
                      (unsigned long long)(bpm_x100 % 100),
                      tenNsPerQn);
    });

    host.begin();

    // ---- ESP32_Host_MIDI USB host wiring ----
    usbMIDI2.setUMPCallback(onUMPFromUsb, (void*)s_umpQueue);
    usbMIDI2.begin();

    Serial.printf("[Ready  ] host MUID = 0x%07X (CI Initiator)\n",
                  host.hostMuid());
    Serial.println();
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------

void loop() {
    // Pump the USB host transport. The lib's begin() spawns a dedicated USB
    // task on core 0 that handles usb_host_* events; task() here is a no-op
    // for the UMP path (UMP is delivered via setUMPCallback directly), but
    // it's idiomatic to call it for symmetry with the MIDI 1.0 fallback.
    usbMIDI2.task();

    // Drain the cross-core queue.
    UMPBatch batch;
    while (xQueueReceive(s_umpQueue, &batch, 0) == pdTRUE) {
        host.feedRx(0, batch.words, batch.count);
    }

    // Tick m2host (Discovery timeouts, scheduled inquiries, etc.).
    host.task();

    // Mount-transition detection. USBMIDI2Connection does not emit lifecycle
    // events directly to user code, so we poll its negotiation state here.
    bool nowM2  = usbMIDI2.isMIDI2();
    bool nowNeg = usbMIDI2.isNegotiated() && nowM2;

    if (nowNeg && !s_wasNegotiated) {
        s_wasNegotiated = true;
        s_retryCount = 0;
        const auto& ep = usbMIDI2.getEndpointInfo();
        host.notifyDeviceMounted(0,
                                  ep.currentProtocol,
                                  ep.numFunctionBlocks,
                                  /*altSettingActive=*/1,
                                  /*bcdMSC=*/0x0200);
    } else if (!nowNeg && s_wasNegotiated) {
        s_wasNegotiated = false;
        s_retryCount = 0;
        host.notifyDeviceUnmounted(0);
    }

    // Defensive auto-retry of Endpoint Discovery. _startNegotiation runs from
    // _processConfig immediately after Alt 1 is claimed; if the device's
    // TinyUSB MIDI 2.0 driver had not finished processing SET_INTERFACE by
    // then, the first Endpoint Discovery is dropped and the cascade stalls.
    // Retry up to 5 times at 2 s intervals, silently. After that, accept the
    // mute device (no MIDI 2.0 negotiation) and let user-level UMP traffic
    // flow if the device chooses to send any.
    if (usbMIDI2.isConnected() && nowM2 && !usbMIDI2.isNegotiated() && s_retryCount < 5) {
        uint32_t nowMs = (uint32_t)millis();
        if ((nowMs - s_lastRetryMs) >= 2000) {
            s_lastRetryMs = nowMs;
            s_retryCount++;
            usbMIDI2.retryNegotiation();
        }
    }
}

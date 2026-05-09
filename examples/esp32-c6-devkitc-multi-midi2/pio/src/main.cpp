// midi2cpp | esp32-c6-devkitc-multi-midi2
//
// ESP32-C6-DevKitC-1 wireless MIDI 2.0 endpoint over BLE + ESP-NOW.
//
//   loop() showcase  ->  midi2::Device  ->  setWriteFn fan-out:
//                                            * MT 0x2 word -> 3 MIDI 1.0 bytes
//                                              -> BLEConnection::sendMidiMessage
//                                              -> ESPNowConnection::sendMidiMessage
//                                            * MT 0x4 word -> downgradeMt4ToMt2
//                                              -> same byte path as above
//
//   BLE central writes -> BLEConnection::setMidiCallback
//                       -> midi2::ByteStreamConverter (group 0)
//                       -> midi2::Device::feedRx -> typed dispatch (Serial)
//
//   ESP-NOW peer writes -> ESPNowConnection::setMidiCallback
//                        -> midi2::ByteStreamConverter (group 1)
//                        -> midi2::Device::feedRx -> typed dispatch (Serial)
//
// The C6 has no USB-OTG hardware, only USB-Serial-JTAG. There is no USB
// MIDI device interface, no PID is consumed. Identity is per-transport:
//   BLE  : advertised name "Esp32C6Multi", standard BLE-MIDI 1.0 service.
//   ESPNW: WiFi channel 1, broadcast peer (FF:FF:FF:FF:FF:FF). Local MAC
//          is read at boot and printed to Serial for unicast pairing.
//
// Both wire transports carry MIDI 1.0 byte streams (BLE-MIDI 1.0 spec,
// ESP-NOW raw bytes). Uplift to UMP MT 0x2 happens locally so that the
// midi2::Device typed dispatch path runs unchanged. Outbound UMP is
// downgraded to MIDI 1.0 bytes before hitting the wire.

#include <Arduino.h>
#include <esp_random.h>
#include <esp_mac.h>

#include <ESP32_Host_MIDI.h>
#include <BLEConnection.h>
#include <ESPNowConnection.h>

#include "midi2_device.h"
#include "midi2.h"

// ---------------------------------------------------------------------------
// Identity constants
// ---------------------------------------------------------------------------

static constexpr const char*    kBleDeviceName   = "Esp32C6Multi";
static constexpr uint8_t        kEspNowChannel   = 1;
static constexpr uint8_t        kGroupBle        = 0;
static constexpr uint8_t        kGroupEspNow     = 1;
static constexpr uint8_t        kShowcaseChannel = 0;     // MIDI channel 1 (zero based)
static constexpr uint32_t       kShowcaseStepMs  = 350;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

BLEConnection           g_ble;
ESPNowConnection        g_espNow;

midi2::Device              g_device;
midi2::ByteStreamConverter g_bleConv(kGroupBle);
midi2::ByteStreamConverter g_espNowConv(kGroupEspNow);

static uint8_t          g_localMac[6] = {0};

// Showcase scale (C major).
static const uint8_t kScale[] = {60, 62, 64, 65, 67, 69, 71, 72};
static uint8_t       g_scaleIdx     = 0;
static uint8_t       g_lastNote     = 0;
static bool          g_haveLastNote = false;
static uint32_t      g_lastStepMs   = 0;

// ---------------------------------------------------------------------------
// Outbound: UMP -> MIDI 1.0 bytes -> wire transports
// ---------------------------------------------------------------------------

// Extract MIDI 1.0 byte payload from a single MT 0x2 UMP word.
// Returns the number of MIDI bytes (1, 2, or 3); 0 means unsupported status.
static uint8_t mt2WordToBytes(uint32_t word, uint8_t out[3]) {
    uint8_t status = (uint8_t)((word >> 16) & 0xFFu);
    uint8_t data1  = (uint8_t)((word >> 8)  & 0x7Fu);
    uint8_t data2  = (uint8_t)( word        & 0x7Fu);
    uint8_t hi     = status & 0xF0u;

    out[0] = status;
    switch (hi) {
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0:
            out[1] = data1; out[2] = data2; return 3;
        case 0xC0: case 0xD0:
            out[1] = data1; return 2;
        case 0xF0:
            switch (status) {
                case 0xF1: case 0xF3: out[1] = data1; return 2;
                case 0xF2: out[1] = data1; out[2] = data2; return 3;
                case 0xF6: case 0xF8: case 0xFA: case 0xFB:
                case 0xFC: case 0xFE: case 0xFF: return 1;
                default: return 0;
            }
        default:
            return 0;
    }
}

static void sendBytesAllTransports(const uint8_t* data, uint8_t len) {
    if (len == 0) return;
    if (g_ble.isConnected())    g_ble.sendMidiMessage(data, len);
    if (g_espNow.isConnected()) g_espNow.sendMidiMessage(data, len);
}

static void onUmpFromDevice(const uint32_t* words, size_t count) {
    size_t i = 0;
    while (i < count) {
        uint8_t mt = (uint8_t)((words[i] >> 28) & 0x0Fu);
        uint8_t bytes[3];

        if (mt == MIDI2_MT_MIDI1_CV) {
            uint8_t n = mt2WordToBytes(words[i], bytes);
            sendBytesAllTransports(bytes, n);
            i += 1;
        } else if (mt == MIDI2_MT_MIDI2_CV) {
            uint32_t mt2Word = 0;
            uint8_t  mt2Count = 0;
            if (midi2::Device::downgradeMt4ToMt2(&words[i], 2, &mt2Word, &mt2Count) && mt2Count == 1) {
                uint8_t n = mt2WordToBytes(mt2Word, bytes);
                sendBytesAllTransports(bytes, n);
            }
            i += 2;
        } else if (mt == MIDI2_MT_SYSTEM) {
            uint8_t n = mt2WordToBytes(words[i], bytes);
            sendBytesAllTransports(bytes, n);
            i += 1;
        } else {
            // Skip past UMP types we do not surface on the MIDI 1.0 wire
            // (SysEx7/8, Flex Data, UMP Stream). Word counts per spec.
            switch (mt) {
                case MIDI2_MT_SYSEX7:    i += 2; break;
                case MIDI2_MT_DATA128:   i += 4; break;
                case MIDI2_MT_FLEX_DATA: i += 4; break;
                case MIDI2_MT_STREAM:    i += 4; break;
                default:                 i += 1; break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Inbound: MIDI 1.0 bytes from the wire -> ByteStreamConverter -> Device.feedRx
// ---------------------------------------------------------------------------

static void feedConverter(midi2::ByteStreamConverter& conv, const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        conv.feed(data[i]);
    }
}

static void onBleRx(void* /*ctx*/, const uint8_t* data, size_t length) {
    feedConverter(g_bleConv, data, length);
}

static void onEspNowRx(void* /*ctx*/, const uint8_t* data, size_t length) {
    feedConverter(g_espNowConv, data, length);
}

// ---------------------------------------------------------------------------
// midi2::Device typed callbacks (Serial trace)
// ---------------------------------------------------------------------------

static const char* groupTag(uint8_t group) {
    switch (group) {
        case kGroupBle:    return "BLE  ";
        case kGroupEspNow: return "ESPNW";
        default:           return "L-OUT";
    }
}

static void wireDeviceCallbacks() {
    g_device.onNoteOn([](uint8_t g, uint8_t ch, uint8_t note, uint16_t vel,
                         uint8_t /*at*/, uint16_t /*ad*/) {
        Serial.printf("[NoteOn  ] %s g=%u ch=%-2u note=%-3u vel=0x%04X\n",
                      groupTag(g), g, ch + 1, note, vel);
    });
    g_device.onNoteOff([](uint8_t g, uint8_t ch, uint8_t note, uint16_t vel,
                          uint8_t /*at*/, uint16_t /*ad*/) {
        Serial.printf("[NoteOff ] %s g=%u ch=%-2u note=%-3u vel=0x%04X\n",
                      groupTag(g), g, ch + 1, note, vel);
    });
    g_device.onCC([](uint8_t g, uint8_t ch, uint8_t idx, uint32_t val) {
        Serial.printf("[CC #%-3u ] %s g=%u ch=%-2u val=0x%08X\n",
                      idx, groupTag(g), g, ch + 1, val);
    });
    g_device.onProgram([](uint8_t g, uint8_t ch, uint8_t prog,
                          uint8_t bMSB, uint8_t bLSB, bool bankValid) {
        if (bankValid)
            Serial.printf("[Program ] %s g=%u ch=%-2u prog=%-3u bank=%u/%u\n",
                          groupTag(g), g, ch + 1, prog, bMSB, bLSB);
        else
            Serial.printf("[Program ] %s g=%u ch=%-2u prog=%-3u\n",
                          groupTag(g), g, ch + 1, prog);
    });
    g_device.onPitchBend([](uint8_t g, uint8_t ch, uint32_t val) {
        Serial.printf("[PitchBnd] %s g=%u ch=%-2u val=0x%08X\n",
                      groupTag(g), g, ch + 1, val);
    });
    g_device.onChannelPressure([](uint8_t g, uint8_t ch, uint32_t val) {
        Serial.printf("[ChnPres ] %s g=%u ch=%-2u val=0x%08X\n",
                      groupTag(g), g, ch + 1, val);
    });
}

static void wireConverters() {
    g_bleConv.onUmp([](const uint32_t* words, uint8_t count) {
        g_device.feedRx(words, count);
    });
    g_espNowConv.onUmp([](const uint32_t* words, uint8_t count) {
        g_device.feedRx(words, count);
    });
}

static void wireWireRxCallbacks() {
    g_ble.setMidiCallback(onBleRx, nullptr);
    g_espNow.setMidiCallback(onEspNowRx, nullptr);
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=====================================================");
    Serial.println("  midi2cpp  |  ESP32-C6-DevKitC-1  |  BLE + ESP-NOW");
    Serial.println("=====================================================");
    Serial.println("  ESP32_Host_MIDI v6.0.0 (BLE + ESP-NOW transports)");
    Serial.println("  midi2cpp Device (typed UMP dispatch, MT 0x2 path)");
    Serial.println();

    g_device.setWriteFn(onUmpFromDevice);
    g_device.setNowFn([]() -> uint32_t { return (uint32_t)millis(); });
    g_device.setMounted(true);
    g_device.setAltSetting(0);
    wireDeviceCallbacks();
    wireConverters();
    g_device.begin();

    Serial.println("[BLE  ] starting advertiser...");
    g_ble.begin(kBleDeviceName);
    Serial.printf("[BLE  ] advertised name = \"%s\"\n", kBleDeviceName);
    Serial.println("[BLE  ] service UUID    = 03B80E5A-EDE8-4B33-A751-6CE34EC4C700");

    Serial.println("[ESPNW] starting...");
    bool espNowOk = g_espNow.begin(kEspNowChannel);
    g_espNow.getLocalMAC(g_localMac);
    Serial.printf("[ESPNW] begin() = %s, channel = %u\n",
                  espNowOk ? "ok" : "FAIL", kEspNowChannel);
    Serial.printf("[ESPNW] local MAC     = %02X:%02X:%02X:%02X:%02X:%02X\n",
                  g_localMac[0], g_localMac[1], g_localMac[2],
                  g_localMac[3], g_localMac[4], g_localMac[5]);
    Serial.println("[ESPNW] no peers added: broadcasts on FF:FF:FF:FF:FF:FF");

    wireWireRxCallbacks();

    Serial.println();
    Serial.println("[Ready ] showcase loop emits a C major scale every 350 ms");
    Serial.println("[Ready ] on both transports. Incoming MIDI 1.0 bytes are");
    Serial.println("[Ready ] uplifted to UMP MT 0x2 and dispatched.");
    Serial.println();
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------

static void showcaseStep() {
    uint32_t now = (uint32_t)millis();
    if ((now - g_lastStepMs) < kShowcaseStepMs) return;
    g_lastStepMs = now;

    if (g_haveLastNote) {
        g_device.sendNoteOff1(kGroupBle, kShowcaseChannel, g_lastNote, 0);
        g_haveLastNote = false;
    }

    uint8_t note = kScale[g_scaleIdx];
    g_device.sendNoteOn1(kGroupBle, kShowcaseChannel, note, 96);
    g_lastNote     = note;
    g_haveLastNote = true;

    g_scaleIdx = (uint8_t)((g_scaleIdx + 1) % (sizeof(kScale) / sizeof(kScale[0])));

    uint8_t cc = (uint8_t)(g_scaleIdx * 16);
    g_device.sendCC1(kGroupBle, kShowcaseChannel, 1, cc);
}

void loop() {
    g_ble.task();
    g_espNow.task();
    g_device.task();

    showcaseStep();

    delay(2);
}

// midi2cpp | t-display-s3-shield-host-midi2
//
// USB MIDI 2.0 Host on the LilyGo T-Display S3 + LilyGo MIDI Shield V1.1,
// with a didactic on-board ST7789 piano roll + MIDI 2.0 event log.
// Released-grade path, no TinyUSB fork, no upstream PR override.
//
//   USB cable from upstream MIDI 2.0 device
//      |
//      v   USB-A (Shield)  ->  internal D+/D- shared bus
//   USB-C IN (Shield) -> T-Display S3 USB-C -> ESP32-S3 USB-OTG (DWC2 FS)
//      |
//      v
//   ESP32_Host_MIDI v5.2.1+ USBMIDI2Connection (ESP-IDF native usb_host_*)
//      |
//      v   FreeRTOS queue (cross-core handover, USB task -> main loop)
//      |
//      v
//   midi2cpp m2host (typed dispatch + MIDI-CI Initiator)
//      |
//      v
//   piano_display (this recipe, src/piano_display.cpp)
//      * 25-key roll on the on-board ST7789, ~60 fps
//      * info bar with identity, device info, latest typed event,
//        per-category counters, view range
//
// Released libraries only (post v5.2.1 ESP32_Host_MIDI). Every typed
// event is also mirrored to the UART log for cross-checking.

// ESP32_HOST_MIDI_NO_USB_HOST is defined globally in platformio.ini build_flags.

#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <ESP32_Host_MIDI.h>
#include <USBMIDI2Connection.h>

#include "midi2_host.h"
#include "piano_display.h"

using piano_display::Category;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

USBMIDI2Connection usbMIDI2;
midi2::m2host      host;

struct UMPBatch {
    uint32_t words[8];
    uint8_t  count;
};
static QueueHandle_t s_umpQueue = nullptr;

static bool      s_wasNegotiated = false;
static uint32_t  s_lastRetryMs   = 0;
static uint8_t   s_retryCount    = 0;

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
// Helpers
// ---------------------------------------------------------------------------

static const char* note_name(uint8_t note, char buf[8]) {
    static const char* names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int oct = (int)(note / 12) - 1;
    std::snprintf(buf, 8, "%s%d", names[note % 12], oct);
    return buf;
}

static void show_event(Category cat, const char* fmt, ...) {
    char body[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    piano_display::set_last_event(cat, body);
    piano_display::bump_counter(cat);
}

// ---------------------------------------------------------------------------
// Identity printer (UART) + display surfacing
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

    piano_display::set_status(id.supportsMidi2Protocol ? "MIDI 2.0 negotiated"
                                                       : "negotiating...");
    piano_display::set_device_info(id.endpointName,
                                   id.ciMuid,
                                   id.ciDiscovered);
}

// ---------------------------------------------------------------------------
// Piano render task (60 fps, core 1)
// ---------------------------------------------------------------------------

static void pianoRenderTask(void* /*arg*/) {
    // 30 fps. The bigger info bar (5 lines of text) now spends more
    // time inside draw_info_bar; halving the render rate gives the
    // pushSprite (320x170 16bpp on parallel-8) enough headroom and
    // avoids visible flicker under MIDI bursts.
    const TickType_t period = pdMS_TO_TICKS(33);
    while (true) {
        piano_display::render_frame();
        vTaskDelay(period);
    }
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=====================================================");
    Serial.println("  midi2cpp  |  T-Display S3 + MIDI Shield  |  USB MIDI 2.0 Host");
    Serial.println("=====================================================");

    piano_display::init();
    piano_display::set_status("waiting for device");
    piano_display::set_device_info(nullptr, 0, false);

    // Priority 3 keeps the render task above arduino-esp32's loop()
    // (priority 1) but below the USB host FreeRTOS task on core 0,
    // so a UMP burst never gets paused mid-transfer to repaint a frame.
    xTaskCreatePinnedToCore(pianoRenderTask, "piano",
                            4096, nullptr, 3, nullptr, 1);

    s_umpQueue = xQueueCreate(32, sizeof(UMPBatch));

    // ---- midi2cpp host wiring ----
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
        piano_display::set_status("waiting for device");
        piano_display::set_device_info(nullptr, 0, false);
        piano_display::clear_active_notes();
    });
    host.onIdentityUpdated([](uint8_t idx,
                              const midi2::Host::DeviceIdentity& id) {
        printIdentity(idx, id);
    });

    // ---- typed UMP dispatch -> piano + log + counters ----

    host.onNoteOn([](uint8_t idx, uint8_t group, uint8_t channel,
                     uint8_t note, uint16_t velocity,
                     uint8_t /*attrType*/, uint16_t /*attrData*/) {
        char nb[8];
        Serial.printf("[NoteOn  ] dev=%u g=%u ch=%-2u note=%-3u vel=0x%04X\n",
                      idx, group, channel + 1, note, velocity);
        piano_display::set_note_active(note, true);
        show_event(Category::NoteOn,
                   "ch=%u %s vel=0x%04X (16-bit)",
                   (unsigned)(channel + 1), note_name(note, nb), velocity);
    });

    host.onNoteOff([](uint8_t idx, uint8_t group, uint8_t channel,
                      uint8_t note, uint16_t /*velocity*/,
                      uint8_t /*attrType*/, uint16_t /*attrData*/) {
        char nb[8];
        Serial.printf("[NoteOff ] dev=%u g=%u ch=%-2u note=%-3u\n",
                      idx, group, channel + 1, note);
        piano_display::set_note_active(note, false);
        show_event(Category::NoteOff,
                   "ch=%u %s",
                   (unsigned)(channel + 1), note_name(note, nb));
    });

    host.onCC([](uint8_t idx, uint8_t group, uint8_t channel,
                 uint8_t index, uint32_t value) {
        Serial.printf("[CC #%-3u ] dev=%u g=%u ch=%-2u val=0x%08X\n",
                      index, idx, group, channel + 1, value);
        show_event(Category::CC,
                   "ch=%u #%u val=0x%08lX (32-bit)",
                   (unsigned)(channel + 1), (unsigned)index,
                   (unsigned long)value);
    });

    host.onPitchBend([](uint8_t idx, uint8_t group, uint8_t channel,
                        uint32_t value) {
        int32_t signedPb = (int32_t)((int64_t)value - (int64_t)0x80000000UL);
        Serial.printf("[PitchBnd] dev=%u g=%u ch=%-2u val=%+ld\n",
                      idx, group, channel + 1, (long)signedPb);
        show_event(Category::PitchBend,
                   "ch=%u %+ld (32-bit signed)",
                   (unsigned)(channel + 1), (long)signedPb);
    });

    host.onChannelPressure([](uint8_t idx, uint8_t group, uint8_t channel,
                              uint32_t value) {
        Serial.printf("[ChnPres ] dev=%u g=%u ch=%-2u val=0x%08X\n",
                      idx, group, channel + 1, value);
        show_event(Category::ChnPressure,
                   "ch=%u val=0x%08lX (32-bit)",
                   (unsigned)(channel + 1), (unsigned long)value);
    });

    host.onPolyPressure([](uint8_t idx, uint8_t group, uint8_t channel,
                           uint8_t note, uint32_t value) {
        char nb[8];
        Serial.printf("[PolyPres] dev=%u g=%u ch=%-2u note=%-3u val=0x%08X\n",
                      idx, group, channel + 1, note, value);
        show_event(Category::PolyPressure,
                   "ch=%u %s val=0x%08lX (32-bit)",
                   (unsigned)(channel + 1), note_name(note, nb),
                   (unsigned long)value);
    });

    host.onPerNotePitchBend([](uint8_t idx, uint8_t group, uint8_t channel,
                               uint8_t note, uint32_t value) {
        char nb[8];
        int32_t signedPb = (int32_t)((int64_t)value - (int64_t)0x80000000UL);
        Serial.printf("[PerNotPB] dev=%u g=%u ch=%-2u note=%-3u val=%+ld\n",
                      idx, group, channel + 1, note, (long)signedPb);
        show_event(Category::PerNotePB,
                   "ch=%u %s %+ld (per-note)",
                   (unsigned)(channel + 1), note_name(note, nb),
                   (long)signedPb);
    });

    auto pnctrlHandler = [](uint8_t idx, uint8_t group, uint8_t channel,
                            uint8_t note, uint8_t index, uint32_t value) {
        char nb[8];
        Serial.printf("[PerNotCt] dev=%u g=%u ch=%-2u note=%-3u #%u val=0x%08X\n",
                      idx, group, channel + 1, note, index, value);
        show_event(Category::PerNoteCtrl,
                   "ch=%u %s #%u val=0x%08lX",
                   (unsigned)(channel + 1), note_name(note, nb),
                   (unsigned)index, (unsigned long)value);
    };
    host.onRegPerNoteController(pnctrlHandler);
    host.onAsnPerNoteController(pnctrlHandler);

    host.onProgram([](uint8_t idx, uint8_t group, uint8_t channel,
                      uint8_t program, uint8_t bMSB, uint8_t bLSB,
                      bool bankValid) {
        if (bankValid) {
            Serial.printf("[Program ] dev=%u g=%u ch=%-2u prog=%-3u bank=%u/%u\n",
                          idx, group, channel + 1, program, bMSB, bLSB);
            show_event(Category::Program, "ch=%u prog=%u bank=%u/%u",
                       (unsigned)(channel + 1), (unsigned)program,
                       (unsigned)bMSB, (unsigned)bLSB);
        } else {
            Serial.printf("[Program ] dev=%u g=%u ch=%-2u prog=%-3u\n",
                          idx, group, channel + 1, program);
            show_event(Category::Program, "ch=%u prog=%u",
                       (unsigned)(channel + 1), (unsigned)program);
        }
    });

    host.onSysEx7([](uint8_t idx, uint8_t group,
                     const uint8_t* /*data*/, uint16_t len) {
        Serial.printf("[SysEx7  ] dev=%u g=%u len=%u\n", idx, group, len);
        show_event(Category::SysEx, "MT3 SysEx7 len=%u", (unsigned)len);
    });

    host.onSysEx8([](uint8_t idx, uint8_t group, uint8_t streamId,
                     const uint8_t* /*data*/, uint16_t len) {
        Serial.printf("[SysEx8  ] dev=%u g=%u stream=%u len=%u\n",
                      idx, group, streamId, len);
        show_event(Category::SysEx, "MT5 SysEx8 stream=%u len=%u",
                   (unsigned)streamId, (unsigned)len);
    });

    host.onTempo([](uint8_t idx, uint8_t group, uint32_t tenNsPerQn) {
        uint64_t bpm_x100 = (uint64_t)6000000000ULL / (uint64_t)tenNsPerQn;
        Serial.printf("[Tempo   ] dev=%u g=%u  %llu.%02llu BPM (10ns/qn=%u)\n",
                      idx, group,
                      (unsigned long long)(bpm_x100 / 100),
                      (unsigned long long)(bpm_x100 % 100),
                      tenNsPerQn);
        show_event(Category::FlexData, "Tempo %llu.%02llu BPM",
                   (unsigned long long)(bpm_x100 / 100),
                   (unsigned long long)(bpm_x100 % 100));
    });

    host.begin();

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
    usbMIDI2.task();

    UMPBatch batch;
    while (xQueueReceive(s_umpQueue, &batch, 0) == pdTRUE) {
        host.feedRx(0, batch.words, batch.count);
    }

    host.task();

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

    if (usbMIDI2.isConnected() && nowM2 && !usbMIDI2.isNegotiated() && s_retryCount < 5) {
        uint32_t nowMs = (uint32_t)millis();
        if ((nowMs - s_lastRetryMs) >= 2000) {
            s_lastRetryMs = nowMs;
            s_retryCount++;
            usbMIDI2.retryNegotiation();
        }
    }
}

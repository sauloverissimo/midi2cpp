/*
 * main.cpp, t-display-s3-midi2-receiver.
 *
 * USB MIDI 2.0 device receiver with on-board piano visualisation on the
 * LilyGo T-Display S3 (ESP32-S3 + ST7789 1.9" 320x170 IPS, parallel 8-bit).
 *
 * What this is:
 *   The host (DAW, OS, another midi2cpp host recipe) sends UMP, the
 *   T-Display S3 mirrors the note activity on the on-board piano roll.
 *   The recipe is a receiver showcase: it does NOT emit notes, it does
 *   NOT play sound, it does NOT generate music. It is a visual debugger
 *   for what the host is actually sending over USB MIDI 2.0.
 *
 * What it does respond to (well-formed device behaviour):
 *   - UMP Stream Discovery responder (Endpoint Info, Device Identity,
 *     Endpoint Name, Product Instance ID, Stream Config Notify, FB Info,
 *     FB Name)
 *   - MIDI-CI Discovery + PE Capability + PE Get auto-replied via
 *     m2ci's Appendix E convenience responder
 *   - JR Timestamp heartbeat every 500 ms (MT 0x0 status 0x2)
 *
 * What it visualises:
 *   - Note On / Off (MIDI 1.0 channel voice + MIDI 2.0 channel voice +
 *     Note On with Attribute) -> key lit / unlit on the piano roll
 *   - Auto-shift octave: when an incoming note falls outside the visible
 *     range, the piano rerenders centred on the active region
 *   - Info bar: identity, USB lifecycle, UMP counters per category
 */
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "t_display_s3_midi2.h"
#include "piano_display.h"

using namespace midi2;

/*--------------------------------------------------------------------+
 * Identity
 *--------------------------------------------------------------------*/
static const uint8_t  kMfrId[3]        = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId        = 0x0001;
static const uint16_t kModelId         = 0x0001;
static const uint32_t kVersion         = 0x00010000;
static const char     kEndpointName[]  = "TDisplayS3";
static const char     kProductInstId[] = "TDisplayS3-receiver-0001";
static const char     kFbName[]        = "Main";

/*--------------------------------------------------------------------+
 * UMP Stream responder
 *--------------------------------------------------------------------*/
static void install_stream_responder(m2device& midi) {
    midi.onEndpointDiscovery([&midi](uint8_t filter) {
        ESP_LOGI("stream", "Endpoint Discovery filter=0x%02X", filter);
        if (filter & 0x01) {
            midi.sendEndpointInfo(/*ump_ver*/ 1, 1,
                                  /*static_fb*/ true, /*num_fb*/ 1,
                                  /*midi2*/ true, /*midi1*/ true,
                                  /*rx_jr*/ true, /*tx_jr*/ true);
        }
        if (filter & 0x02) midi.sendDeviceIdentity(kMfrId, kFamilyId, kModelId, kVersion);
        if (filter & 0x04) midi.sendEndpointNameUpdate(kEndpointName);
        if (filter & 0x08) midi.sendProductInstanceIdUpdate(kProductInstId);
        if (filter & 0x10) midi.sendStreamConfigNotify(/*protocol*/ 0x02);
    });
    midi.onFbDiscovery([&midi](uint8_t fbNum, uint8_t filter) {
        uint8_t target = (fbNum == 0xFF) ? 0 : fbNum;
        if (target != 0) return;
        if (filter & 0x01) {
            midi.sendFbInfo(/*active*/ true, /*fb_num*/ 0,
                            /*direction*/ 0x03, /*first_group*/ 0,
                            /*num_groups*/ 1, /*midi_ci_ver*/ 0x02,
                            /*sysex8*/ false, /*protocol*/ 0x02);
        }
        if (filter & 0x02) midi.sendFbNameUpdate(0, kFbName);
    });
    midi.onStreamConfigRequest([&midi](uint8_t protocol) {
        midi.sendStreamConfigNotify(protocol);
    });
}

/*--------------------------------------------------------------------+
 * Inbound UMP -> piano UI bridge
 *--------------------------------------------------------------------*/
static void install_receiver_callbacks(m2device& midi) {
    // MIDI 2.0 channel voice (MT 0x4): full-resolution Note On / Off.
    midi.onNoteOn([](uint8_t ch, uint8_t note, uint16_t vel) {
        ESP_LOGI("rx", "NoteOn  ch=%u note=%u vel=0x%04X",
                 (unsigned)ch, (unsigned)note, (unsigned)vel);
        piano_display::set_note_active(note, true);
        piano_display::bump_counter(piano_display::Counter::NoteOn);
    });
    midi.onNoteOff([](uint8_t ch, uint8_t note, uint16_t vel) {
        (void)vel;
        ESP_LOGI("rx", "NoteOff ch=%u note=%u", (unsigned)ch, (unsigned)note);
        piano_display::set_note_active(note, false);
        piano_display::bump_counter(piano_display::Counter::NoteOff);
    });
    midi.onCC([](uint8_t ch, uint8_t idx, uint32_t val) {
        (void)ch; (void)idx; (void)val;
        piano_display::bump_counter(piano_display::Counter::CC);
    });
    midi.onPitchBend([](uint8_t ch, uint32_t val) {
        (void)ch; (void)val;
        piano_display::bump_counter(piano_display::Counter::PitchBend);
    });
}

/*--------------------------------------------------------------------+
 * FreeRTOS entry point
 *--------------------------------------------------------------------*/
extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI("boot", "================================================");
    ESP_LOGI("boot", "  t-display-s3-midi2-receiver  (VID:PID 0xCAFE:0x4094)");
    ESP_LOGI("boot", "================================================");

    static m2device midi;
    static m2ci     ci(midi);

    t_display_s3_midi2::init(midi, ci);
    midi.begin();
    midi.enableJRHeartbeat(500);
    ci.begin(kMfrId, kFamilyId, kModelId, kVersion);

    install_stream_responder(midi);
    install_receiver_callbacks(midi);

    ESP_LOGI("boot", "ready, entering receiver loop");

    bool prev_mounted = false;
    while (true) {
        t_display_s3_midi2::task(midi);

        bool mounted = midi.isMounted();
        if (mounted != prev_mounted) {
            t_display_s3_midi2::show_mounted(mounted);
            prev_mounted = mounted;
        }

        vTaskDelay(1);
    }
}

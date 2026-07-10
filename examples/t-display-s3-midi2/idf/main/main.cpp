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

#include "board_midi2.h"
#include "piano_display.h"

using namespace midi2;

/*--------------------------------------------------------------------+
 * Identity
 *--------------------------------------------------------------------*/
static const uint8_t  kMfrId[3]        = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId        = 0x0001;
static const uint16_t kModelId         = 0x0011;
static const uint32_t kVersion         = 0x00010000;

/*--------------------------------------------------------------------+
 * UMP Stream Discovery is answered by the TinyUSB built-in responder
 * (PR #3738): Endpoint Name from CFG_TUD_MIDI2_EP_NAME, FB direction +
 * group span from tud_midi2_gtb_desc_cb, FB name from tud_midi2_fb_name_cb
 * (in the board glue). No app-side stream responder is installed; Device
 * Identity is carried by MIDI-CI Discovery (SysEx) via ci.begin.
 *--------------------------------------------------------------------*/

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

    midi2_board::init(midi, ci);
    midi.begin();
    midi.enableJRHeartbeat(500);
    ci.begin(kMfrId, kFamilyId, kModelId, kVersion);
    ci.addPropertyStatic("DeviceInfo",
        "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[17,0],\"versionId\":[0,0,4,0],"
         "\"manufacturer\":\"midi2.diy\","
         "\"family\":\"ESP32-S3\","
         "\"model\":\"LILYGO T-Display S3 MIDI 2.0\","
         "\"version\":\"0.0.1\"}");
    ci.addPropertyStatic("ChannelList",
        "[{\"title\":\"Main\",\"channel\":1}]");
    ci.addPropertyStatic("ProgramList",
        "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]");

    install_receiver_callbacks(midi);

    ESP_LOGI("boot", "ready, entering receiver loop");

    bool prev_mounted = false;
    while (true) {
        midi2_board::task(midi);

        bool mounted = midi.isMounted();
        if (mounted != prev_mounted) {
            midi2_board::show_mounted(mounted);
            prev_mounted = mounted;
        }

        vTaskDelay(1);
    }
}

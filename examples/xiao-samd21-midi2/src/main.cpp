/*
 * main.cpp, xiao-samd21-midi2-showcase.
 *
 * Minimal-core USB MIDI 2.0 device showcase on the Seeed XIAO
 * SAMD21 (ATSAMD21G18A, 32 KB SRAM, 256 KB flash). Smaller scope than
 * the rp2040-midi2 reference because of the SAMD21's tight resources.
 *
 * Boot (once):
 *   - UMP Stream Discovery responder (Endpoint Info, Device Identity,
 *     Endpoint Name, Product Instance ID, Stream Config Notify, FB
 *     Info, FB Name)
 *   - MIDI-CI Discovery auto-replied via m2ci's Appendix E convenience
 *     responder. No Profile Configuration, no Property Exchange storage,
 *     Process Inquiry MIDI report and GM 1 profile registered.
 *
 * Always-on:
 *   - JR Timestamp heartbeat every 500 ms (MT 0x0 status 0x2)
 *   - On-board yellow LED (PA17): lit while USB is mounted
 *
 * Per cycle (~4.5 s):
 *   - Chromatic walk C4 to G#4 (8 steps, 500 ms each) with 16-bit
 *     velocity ramp 0x2000 to 0xFFFF and 32-bit CC #74 sweep
 *   - 2 s gap before next cycle
 */
#include "tusb.h"          // tusb_time_millis_api()

#include "board_midi2.h"

using namespace midi2;

/*--------------------------------------------------------------------+
 * Identity
 *--------------------------------------------------------------------*/
static const uint8_t  kMfrId[3]        = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId        = 0x0001;
static const uint16_t kModelId         = 0x000C;
static const uint32_t kVersion         = 0x00010000;

/*--------------------------------------------------------------------+
 * UMP Stream Discovery is answered by the TinyUSB built-in responder
 * (PR #3738): Endpoint Name from CFG_TUD_MIDI2_EP_NAME, FB direction +
 * group span from tud_midi2_gtb_desc_cb, FB name from tud_midi2_fb_name_cb
 * (all in board_midi2.cpp). No app-side stream responder is
 * installed; Device Identity is carried by MIDI-CI Discovery via ci.begin.
 *--------------------------------------------------------------------*/

/*--------------------------------------------------------------------+
 * Demo cycle
 *--------------------------------------------------------------------*/
constexpr uint8_t  kCh           = 0;
constexpr uint8_t  kBaseNote     = 60;     // C4
constexpr uint8_t  kStepCount    = 8;
constexpr uint32_t kStepMs       = 500;
constexpr uint32_t kCycleGapMs   = 2000;

struct Showcase {
    uint8_t  step       = 0;
    uint32_t last_ms    = 0;
    bool     in_cycle   = false;
    uint32_t gap_until  = 0;
};


// MT 0x3/0x5 coverage: SysEx7 identity, then SysEx8 and one Mixed Data Set
// chunk (single stream id, manufacturer SysEx id 0x7D).
static void send_data_coverage(midi2::m2device& midi) {
    static const uint8_t sx7[] = {0x7E, 0x7F, 0x06, 0x02, 0x7D, 0x01, 0x00, 0x40,
                                  0x00, 0x04, 0x00, 0x00};
    midi.sendSysEx7(0, sx7, sizeof sx7);
    static const uint8_t sx8[] = {0x7D, 0x01, 0x02, 0x03, 0x04};
    midi.sendSysEx8(0, /*streamId*/ 0, sx8, sizeof sx8);
    static const uint8_t mdsData[] = {0x7D, 0x4D, 0x44, 0x53};
    midi.sendMds(0, /*mdsId*/ 1, mdsData, sizeof mdsData, /*mfrId*/ 0x7D00);
}

static void showcase_step(m2device& midi, Showcase& s) {
    if (!midi.isMounted() || midi.altSetting() != 1) return;

    uint32_t now = (uint32_t)tusb_time_millis_api();

    if (!s.in_cycle && now < s.gap_until) return;

    if (s.step < kStepCount &&
        (s.step == 0 || (now - s.last_ms) >= kStepMs)) {
        if (s.step > 0) {
            midi.noteOff(kCh, (uint8_t)(kBaseNote + s.step - 1));
        }
        uint8_t  note   = (uint8_t)(kBaseNote + s.step);
        uint16_t vel    = (uint16_t)(0x2000u + (uint32_t)s.step *
                                     ((0xFFFFu - 0x2000u) / (kStepCount - 1)));
        uint32_t cc_val = 0x20000000u + (uint32_t)s.step *
                          ((0xFFFFFFFFu - 0x20000000u) / (kStepCount - 1));

        midi.noteOn(kCh, note, vel);
        midi.cc(kCh, /*idx*/ 74, cc_val);

        s.last_ms = now;
        s.step++;
        s.in_cycle = true;
    }

    if (s.in_cycle && s.step == kStepCount && (now - s.last_ms) >= kStepMs) {
        midi.noteOff(kCh, (uint8_t)(kBaseNote + kStepCount - 1));
        send_data_coverage(midi);
        s.step      = 0;
        s.in_cycle  = false;
        s.gap_until = now + kCycleGapMs;
    }
}

/*--------------------------------------------------------------------+
 * Main
 *--------------------------------------------------------------------*/
int main() {
    static m2device midi;
    static m2ci     ci(midi);

    midi2_board::init(midi, ci);
    midi.begin();
    midi.enableJRHeartbeat(500);
    ci.begin(kMfrId, kFamilyId, kModelId, kVersion);
    ci.addPropertyStatic("DeviceInfo",
        "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[12,0],\"versionId\":[0,0,4,0],"
         "\"manufacturer\":\"midi2.diy\","
         "\"family\":\"SAMD21\","
         "\"model\":\"XIAO SAMD21 MIDI 2.0\","
         "\"version\":\"0.0.1\"}");
    ci.addPropertyStatic("ChannelList",
        "[{\"title\":\"Main\",\"channel\":1}]");
    ci.addPropertyStatic("ProgramList",
        "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]");

    // Advertised in ci_cat (0x1C): back Profiles with GM 1 and Process
    // Inquiry with a MIDI report, so every advertised category answers.
    static const uint8_t kProfileGm1[5] = {0x7E, 0x00, 0x00, 0x01, 0x00};
    ci.addProfile(kProfileGm1, /*alwaysOn*/ false);
    ci.setMidiReport(0x01, 0x00000000FFFFFFFFull,
                     0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull);

    static Showcase showcase{};
    bool prev_mounted = false;

    while (true) {
        midi2_board::task(midi);

        bool mounted = midi.isMounted();
        if (mounted != prev_mounted) {
            midi2_board::led_show_mounted(mounted);
            prev_mounted = mounted;
        }

        showcase_step(midi, showcase);
    }
}

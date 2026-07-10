/*
 * main.cpp, ra4m1-weact-device-midi2 showcase.
 *
 * Minimal-core USB MIDI 2.0 device showcase on the WeAct Studio
 * RA4M1 64-Pin Core Board (Renesas R7FA4M1AB3CFM, 32 KB SRAM, 256 KB
 * flash). Same scope as the rp2040 reference's small sibling
 * xiao-samd21-midi2 because of the RA4M1's tight SRAM.
 *
 * Boot (once):
 *   - UMP Stream Discovery responder (Endpoint Info, Device Identity,
 *     Endpoint Name, Product Instance ID, Stream Config Notify, FB
 *     Info, FB Name)
 *   - MIDI-CI Discovery auto-replied via m2ci's Appendix E convenience
 *     responder. No Profile Configuration, no Property Exchange storage,
 *     no Process Inquiry advertising (out of scope for this showcase).
 *
 * Always-on:
 *   - JR Timestamp heartbeat every 500 ms (MT 0x0 status 0x2)
 *   - On-board blue LED (P0.12): blinks with the showcase activity;
 *     cleared when USB unmounts
 *
 * Per cycle (~11 s): a MIDI 2.0 feature tour (see the feature tour
 * block below) that walks the Channel Voice features unique to the 2.0
 * protocol, then a 1 s gap before the tour repeats.
 */
#include "tusb.h"          // tusb_time_millis_api()

#include "board_midi2.h"

using namespace midi2;

/*--------------------------------------------------------------------+
 * Identity
 *--------------------------------------------------------------------*/
static const uint8_t  kMfrId[3]        = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId        = 0x0001;
static const uint16_t kModelId         = 0x000B;
static const uint32_t kVersion         = 0x00010000;

/*--------------------------------------------------------------------+
 * UMP Stream Discovery is answered by the TinyUSB built-in responder
 * (PR #3738): Endpoint Name from CFG_TUD_MIDI2_EP_NAME, FB direction +
 * group span from tud_midi2_gtb_desc_cb, FB name from tud_midi2_fb_name_cb
 * (in the board glue). No app-side stream responder is installed; Device
 * Identity is carried by MIDI-CI Discovery (SysEx) via ci.begin.
 *--------------------------------------------------------------------*/

/*--------------------------------------------------------------------+
 * MIDI 2.0 feature tour
 *
 * Each window exercises a Channel Voice feature that MIDI 1.0 cannot
 * express, so a host capture shows what the 2.0 protocol adds over a
 * legacy 1.0 stream:
 *   1. Per-Note Pitch Bend   a chord whose notes each bend on their own
 *   2. Per-Note Controller   per-note brightness (registered #74)
 *   3. High resolution       32-bit CC sweep + 32-bit channel pitch bend
 *   4. RPN / NRPN / Program  atomic 32-bit controllers + bank select
 *   5. Poly Pressure + Attr  16-bit velocity, note attribute, 32-bit AT
 * The phases run back to back, then a short gap, then the tour repeats.
 *--------------------------------------------------------------------*/
constexpr uint8_t  kGroup    = 0;
constexpr uint8_t  kCh       = 0;
constexpr uint8_t  kChord[]  = {60, 64, 67, 71};   // Cmaj7: C4 E4 G4 B4
constexpr uint8_t  kChordN   = sizeof(kChord);
constexpr uint32_t kCenterPB = 0x80000000u;        // 32-bit pitch-bend center

enum Phase : uint8_t {
    PH_PNPB = 0, PH_PNCTL, PH_HIRES, PH_RPNPROG, PH_POLYATTR, PH_GAP, PH_COUNT
};
constexpr uint32_t kPhaseMs[PH_COUNT] = {2200, 2000, 2000, 2200, 2000, 1000};
constexpr uint32_t kTickMs = 60;                   // modulation update interval

struct Showcase {
    uint8_t  phase     = PH_COUNT;   // PH_COUNT = not started; forces enter
    uint32_t phase_ms  = 0;
    uint32_t last_tick = 0;
    uint16_t tick      = 0;
    bool     led_on    = false;
};

static inline void blink(Showcase& s) {
    s.led_on = !s.led_on;
    midi2_board::led(s.led_on);
}

// Triangle wave: phase 0..0xFFFF rises to the peak at mid-phase, falls back.
static inline uint16_t tri16(uint16_t ph) {
    return (ph < 0x8000) ? (uint16_t)(ph << 1) : (uint16_t)((uint16_t)(0xFFFFu - ph) << 1);
}

static void phase_enter(m2device& midi, Showcase& s) {
    switch (s.phase) {
        case PH_PNPB:
        case PH_PNCTL:
            for (uint8_t i = 0; i < kChordN; ++i)
                midi.sendNoteOn(kGroup, kCh, kChord[i], (uint16_t)(0x9000u + i * 0x1800u));
            blink(s);
            break;
        case PH_HIRES:
            midi.sendNoteOn(kGroup, kCh, 60, 0xE000);
            blink(s);
            break;
        case PH_RPNPROG:
            // Program Change with an atomic Bank Select (MIDI 1.0 needs
            // two CCs + PC; MIDI 2.0 carries it in one message).
            midi.sendProgram(kGroup, kCh, /*program*/ 5,
                             /*bankMSB*/ 0, /*bankLSB*/ 2, /*bankValid*/ true);
            blink(s);
            break;
        case PH_POLYATTR:
            // 16-bit velocity + a note attribute (type 3 = Pitch 7.9).
            midi.sendNoteOn(kGroup, kCh, 60, 0xF000, /*attrType*/ 3, /*attrData*/ 0x8000);
            blink(s);
            break;
        case PH_GAP:
            midi2_board::led(false);
            s.led_on = false;
            break;
        default: break;
    }
}

static void phase_tick(m2device& midi, Showcase& s, uint32_t now) {
    uint32_t dur     = kPhaseMs[s.phase];
    uint32_t elapsed = now - s.phase_ms;
    if (elapsed > dur) elapsed = dur;
    uint16_t frac = (uint16_t)((uint32_t)elapsed * 0xFFFFu / dur);   // 0..0xFFFF

    switch (s.phase) {
        case PH_PNPB:
            // Each chord note bends on its own triangle, phase + direction
            // per note, so the four pitches move independently.
            for (uint8_t i = 0; i < kChordN; ++i) {
                uint32_t span = (uint32_t)tri16((uint16_t)(frac + i * 0x4000u)) << 13;
                uint32_t bend = (i & 1) ? (kCenterPB + span) : (kCenterPB - span);
                midi.sendPerNotePitchBend(kGroup, kCh, kChord[i], bend);
            }
            break;
        case PH_PNCTL:
            // Per-note brightness; each note offset so the four fan out.
            for (uint8_t i = 0; i < kChordN; ++i) {
                uint32_t v = ((uint32_t)frac << 14) + (uint32_t)i * 0x10000000u;
                midi.sendRegPerNoteController(kGroup, kCh, kChord[i], /*#74*/ 74, v);
            }
            break;
        case PH_HIRES:
            midi.sendCC(kGroup, kCh, 74, (uint32_t)frac << 16);              // 32-bit sweep
            midi.sendPitchBend(kGroup, kCh, kCenterPB + ((uint32_t)tri16(frac) << 13));
            break;
        case PH_RPNPROG:
            if (s.tick == 4)  midi.sendRpn(kGroup, kCh, /*PB sensitivity*/ 0, 0, 0x30000000u);
            if (s.tick == 9)  midi.sendNrpn(kGroup, kCh, /*vendor*/ 1, 8, 0x60000000u);
            if (s.tick == 14) midi.sendNoteOn(kGroup, kCh, 60, 0xC000);
            if (s.tick >= 16) midi.sendPitchBend(kGroup, kCh, kCenterPB + ((uint32_t)frac << 15));
            break;
        case PH_POLYATTR:
            midi.sendPolyPressure(kGroup, kCh, 60, (uint32_t)frac << 16);    // 32-bit AT swell
            break;
        default: break;
    }
    if (s.phase != PH_GAP) blink(s);
}

static void phase_exit(m2device& midi, Showcase& s) {
    switch (s.phase) {
        case PH_PNPB:
        case PH_PNCTL:
            for (uint8_t i = 0; i < kChordN; ++i)
                midi.sendNoteOff(kGroup, kCh, kChord[i], 0);
            break;
        case PH_HIRES:
        case PH_RPNPROG:
            midi.sendNoteOff(kGroup, kCh, 60, 0);
            midi.sendPitchBend(kGroup, kCh, kCenterPB);    // recenter
            break;
        case PH_POLYATTR:
            midi.sendNoteOff(kGroup, kCh, 60, 0);
            break;
        default: break;   // PH_GAP / PH_COUNT: nothing sounding
    }
}

static void showcase_step(m2device& midi, Showcase& s) {
    if (!midi.isMounted() || midi.altSetting() != 1) {
        s.phase = PH_COUNT;     // restart the tour on next mount
        return;
    }
    uint32_t now = (uint32_t)tusb_time_millis_api();

    bool first = (s.phase == PH_COUNT);
    if (first || (now - s.phase_ms) >= kPhaseMs[s.phase]) {
        if (!first) phase_exit(midi, s);
        s.phase     = first ? (uint8_t)PH_PNPB : (uint8_t)((s.phase + 1) % PH_COUNT);
        s.phase_ms  = now;
        s.last_tick = now;
        s.tick      = 0;
        phase_enter(midi, s);
        return;
    }

    if ((now - s.last_tick) >= kTickMs) {
        s.last_tick = now;
        s.tick++;
        phase_tick(midi, s, now);
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
        "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[11,0],\"versionId\":[0,0,4,0],"
         "\"manufacturer\":\"midi2.diy\","
         "\"family\":\"RA4M1\","
         "\"model\":\"WeAct RA4M1 MIDI 2.0\","
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
            // On unmount, clear the LED; while mounted the showcase
            // drives it in time with the notes.
            if (!mounted) {
                midi2_board::led(false);
                showcase.led_on = false;
            }
            prev_mounted = mounted;
        }

        showcase_step(midi, showcase);
    }
}

/*
 * main.cpp, nrf52840-promicro-midi2-showcase.
 *
 * Standard-subset USB MIDI 2.0 device showcase on Pro Micro
 * nRF52840 class boards (Nice!Nano, BlueMicro840, FYSETC nRF52840 Pro
 * Micro, generic clones). nRF52840 Cortex-M4F at 64 MHz, 256 KB SRAM,
 * 1 MB flash, native USB FS.
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
 *   - On-board LED activity via the BSP (P1.15 on Feather; not visible
 *     on most generic Pro Micro clones)
 *
 * Per cycle (~13 s):
 *   - 3.6 s sustained C4 with Per-Note Pitch Bend vibrato (5 Hz, +/-
 *     half a semitone) demonstrating MT 0x4 status 0x6
 *   - Chromatic walk C5 to G#5 (8 steps, 500 ms each) with 16-bit
 *     velocity ramp 0x2000 to 0xFFFF and 32-bit CC #74 sweep
 *   - RPN 0/0 (Pitch Bend Sensitivity), NRPN 0x12/0x34, Relative RPN
 *     +delta, Relative NRPN -delta, demonstrating MT 0x4 status 0x2/0x3/0x4/0x5
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
static const uint16_t kModelId         = 0x000A;
static const uint32_t kVersion         = 0x00010000;

/*--------------------------------------------------------------------+
 * UMP Stream Discovery is answered by the TinyUSB built-in responder
 * (PR #3738): Endpoint Name from CFG_TUD_MIDI2_EP_NAME, FB direction +
 * group span from tud_midi2_gtb_desc_cb, FB name from tud_midi2_fb_name_cb
 * (in the board glue). No app-side stream responder is installed; Device
 * Identity is carried by MIDI-CI Discovery (SysEx) via ci.begin.
 *--------------------------------------------------------------------*/

/*--------------------------------------------------------------------+
 * Demo cycle, state machine.
 *
 * The cycle has four phases that fire in sequence and then pause for
 * kCycleGapMs before restarting. Phases are gated on tusb_time_millis_api(),
 * so the loop is non-blocking and the JR heartbeat keeps streaming
 * regardless of which phase the showcase is in.
 *--------------------------------------------------------------------*/
constexpr uint8_t  kGroup        = 0;
constexpr uint8_t  kCh           = 0;

constexpr uint8_t  kVibratoNote  = 60;     // C4
constexpr uint32_t kVibratoMs    = 3600;   // ~3.6 s sustained
constexpr uint32_t kVibratoStepMs= 50;     // 50 ms per PB update -> 20 Hz update rate
constexpr float    kVibratoHz    = 5.0f;
constexpr int32_t  kVibratoAmp   = 0x10000000;  // ~+/- half a semitone

constexpr uint8_t  kWalkBaseNote = 72;     // C5
constexpr uint8_t  kWalkSteps    = 8;
constexpr uint32_t kWalkStepMs   = 500;

constexpr uint32_t kRpnPhaseMs   = 600;
constexpr uint32_t kCycleGapMs   = 2000;

enum class Phase : uint8_t {
    VibratoStart, VibratoRun, VibratoEnd,
    WalkStep, WalkEnd,
    Rpn, Nrpn, RelRpn, RelNrpn,
    GapStart, Gap,
};

struct Showcase {
    Phase    phase     = Phase::VibratoStart;
    uint32_t phase_ms  = 0;
    uint32_t step_ms   = 0;
    uint8_t  step_idx  = 0;
};

static constexpr int32_t pitchbend_center = 0x80000000;

static int32_t vibrato_pb(uint32_t elapsed_ms) {
    // 5 Hz cosine wave, scaled to kVibratoAmp around the center.
    // Use integer-only math to keep the showcase free of FPU surprises
    // on cold boot (the nRF52840 has hardware FPU but the BSP startup
    // does not enable it for noos builds).
    constexpr uint32_t period_ms = (uint32_t)(1000.0f / kVibratoHz);  // 200 ms
    uint32_t t = elapsed_ms % period_ms;
    // Triangle wave approximation of the sine; close enough for the
    // visual + audible vibrato effect, no float ops needed.
    int32_t tri;
    if (t < period_ms / 2) {
        tri = (int32_t)((int32_t)t * 4 * kVibratoAmp / (int32_t)period_ms - kVibratoAmp);
    } else {
        tri = (int32_t)(3 * kVibratoAmp - (int32_t)t * 4 * kVibratoAmp / (int32_t)period_ms);
    }
    return tri;
}

static void showcase_step(m2device& midi, Showcase& s) {
    if (!midi.isMounted() || midi.altSetting() != 1) return;

    uint32_t now = (uint32_t)tusb_time_millis_api();

    switch (s.phase) {
        case Phase::VibratoStart:
            midi.noteOn(kCh, kVibratoNote, 0xC000);
            s.phase     = Phase::VibratoRun;
            s.phase_ms  = now;
            s.step_ms   = now;
            break;

        case Phase::VibratoRun: {
            if ((now - s.step_ms) >= kVibratoStepMs) {
                int32_t pb = vibrato_pb(now - s.phase_ms);
                midi.sendPerNotePitchBend(kGroup, kCh, kVibratoNote,
                                          (uint32_t)((int64_t)pitchbend_center + pb));
                s.step_ms = now;
            }
            if ((now - s.phase_ms) >= kVibratoMs) {
                s.phase = Phase::VibratoEnd;
            }
            break;
        }

        case Phase::VibratoEnd:
            midi.sendPerNotePitchBend(kGroup, kCh, kVibratoNote,
                                      (uint32_t)pitchbend_center);
            midi.noteOff(kCh, kVibratoNote);
            s.phase    = Phase::WalkStep;
            s.step_idx = 0;
            s.step_ms  = now;
            break;

        case Phase::WalkStep:
            if (s.step_idx == 0 || (now - s.step_ms) >= kWalkStepMs) {
                if (s.step_idx > 0) {
                    midi.noteOff(kCh, (uint8_t)(kWalkBaseNote + s.step_idx - 1));
                }
                if (s.step_idx < kWalkSteps) {
                    uint8_t  note = (uint8_t)(kWalkBaseNote + s.step_idx);
                    uint16_t vel  = (uint16_t)(0x2000u + (uint32_t)s.step_idx *
                                               ((0xFFFFu - 0x2000u) / (kWalkSteps - 1)));
                    uint32_t cc_v = 0x20000000u + (uint32_t)s.step_idx *
                                    ((0xFFFFFFFFu - 0x20000000u) / (kWalkSteps - 1));
                    midi.noteOn(kCh, note, vel);
                    midi.cc(kCh, /*idx*/ 74, cc_v);
                    s.step_ms = now;
                    s.step_idx++;
                } else {
                    s.phase = Phase::WalkEnd;
                }
            }
            break;

        case Phase::WalkEnd:
            // Last note already off via the next-iteration noteOff in WalkStep
            // when step_idx == kWalkSteps; nothing to do here.
            s.phase    = Phase::Rpn;
            s.phase_ms = now;
            break;

        case Phase::Rpn:
            if ((now - s.phase_ms) >= kRpnPhaseMs) {
                // RPN 0/0 = Pitch Bend Sensitivity, +/- 12 semitones (0x18000000 ~= 12 / 96).
                midi.sendRpn(kGroup, kCh, /*msb*/ 0x00, /*lsb*/ 0x00, 0x18000000u);
                s.phase    = Phase::Nrpn;
                s.phase_ms = now;
            }
            break;

        case Phase::Nrpn:
            if ((now - s.phase_ms) >= kRpnPhaseMs) {
                midi.sendNrpn(kGroup, kCh, /*msb*/ 0x12, /*lsb*/ 0x34, 0x40000000u);
                s.phase    = Phase::RelRpn;
                s.phase_ms = now;
            }
            break;

        case Phase::RelRpn:
            if ((now - s.phase_ms) >= kRpnPhaseMs) {
                midi.sendRelRpn(kGroup, kCh, 0x00, 0x00, +0x01000000);
                s.phase    = Phase::RelNrpn;
                s.phase_ms = now;
            }
            break;

        case Phase::RelNrpn:
            if ((now - s.phase_ms) >= kRpnPhaseMs) {
                midi.sendRelNrpn(kGroup, kCh, 0x12, 0x34, -0x01000000);
                s.phase    = Phase::GapStart;
            }
            break;

        case Phase::GapStart:
            s.phase    = Phase::Gap;
            s.phase_ms = now;
            break;

        case Phase::Gap:
            if ((now - s.phase_ms) >= kCycleGapMs) {
                s.phase = Phase::VibratoStart;
            }
            break;
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
        "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[10,0],\"versionId\":[0,0,4,0],"
         "\"manufacturer\":\"midi2.diy\","
         "\"family\":\"nRF52840\","
         "\"model\":\"nRF52840 Pro Micro MIDI 2.0\","
         "\"version\":\"0.0.1\"}");
    ci.addPropertyStatic("ChannelList",
        "[{\"title\":\"Main\",\"channel\":1}]");
    ci.addPropertyStatic("ProgramList",
        "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]");


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

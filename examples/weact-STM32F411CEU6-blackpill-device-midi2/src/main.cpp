/*
 * main.cpp, weact-STM32F411CEU6-blackpill-device-midi2-showcase.
 *
 * Headless full-surface USB MIDI 2.0 device showcase on the WeAct Studio
 * STM32F411CEU6 BlackPill. Cortex-M4F at 84 MHz (25 MHz HSE, BSP PLL
 * config), 128 KB SRAM, 512 KB flash, native OTG_FS device on PA11/PA12.
 * Demonstrates every category of message MIDI 2.0 brings beyond MIDI 1.0,
 * in a ~22 s cycle that loops while mounted.
 *
 * This recipe has no UART stdio retarget (unlike the Pico SDK recipes),
 * so the scenes are observed on the wire (aseqdump / a MIDI 2.0 host), not
 * over a serial console. The vibrato uses integer-only math so the demo
 * never depends on FPU enable state.
 *
 *   Boot (once):
 *     - UMP Stream Discovery is answered by the TinyUSB built-in responder
 *       (PR #3738): Endpoint Name from CFG_TUD_MIDI2_EP_NAME, Function Block
 *       direction from the GTB descriptor (board glue), FB name "Main" from
 *       tud_midi2_fb_name_cb. The app installs no stream responder.
 *     - MIDI-CI Discovery + PE Capability + PE Get auto-replied via
 *       m2ci's Appendix E convenience responder
 *     - 1 Custom Profile registered (id 7D 00 00 01 00)
 *     - 3 Properties: static DeviceInfo, dynamic ChannelList,
 *       subscribable OverlayRate (broadcast to subscribers each cycle)
 *     - Process Inquiry: setMidiReport (system + channel + note bitmaps)
 *
 *   Each cycle (~22 s):
 *     Scene A - Flex Data suite: Tempo, Time Sig, Key Sig, Metronome,
 *               Chord Name (Cmaj7) + Start of Clip
 *     Scene B - Per-Note expression stack (single sustained note):
 *               Per-Note Pitch Bend vibrato + Registered Per-Note
 *               Controller (volume) + Assignable Per-Note Controller
 *               (brightness) + Per-Note Management Reset at end
 *     Scene C - Resolution showcase (chromatic walk): 16-bit variable
 *               velocity + 32-bit CC sweep + 32-bit Pitch Bend ramp +
 *               32-bit Poly Pressure + 32-bit Channel Pressure
 *     Scene D - Program Change with Bank in a single UMP
 *     Scene E - RPN/NRPN 32-bit + Relative RPN/NRPN (incremental)
 *     Scene F - Note On with Attribute pitch_7_9 (microtonal +50 cents)
 *     Scene G - SysEx7 emission (fragmented Universal SysEx Identity Reply)
 *     Scene H - Delta Clockstamp (DCTPQ + delta ticks)
 *     Scene I - Property Exchange Notify (broadcast OverlayRate change
 *               to any current subscribers)
 *     Scene J - End of Clip
 *
 *   Always:
 *     - JR Timestamp heartbeat (500 ms, also a MIDI 2.0-only message)
 *     - On-board LED (PC13): lit while USB is mounted
 */
#include <cstdio>          // snprintf (PE value formatting; no console I/O)
#include <cstring>

#include "tusb.h"          // tusb_time_millis_api()

#include "board_midi2.h"

using namespace midi2;

/*--------------------------------------------------------------------+
 * Identity
 *--------------------------------------------------------------------*/
static const uint8_t  kMfrId[3]      = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId      = 0x0001;
static const uint16_t kModelId       = 0x0009;
static const uint32_t kVersion       = 0x00010000;
static const uint8_t  kProfileId[5]     = {0x7E, 0x00, 0x00, 0x01, 0x00};
// Endpoint Name = CFG_TUD_MIDI2_EP_NAME, Product Instance Id =
// CFG_TUD_MIDI2_PRODUCT_ID (tusb_config.h), FB name "Main" =
// tud_midi2_fb_name_cb, FB direction = GTB descriptor (board glue). The
// TinyUSB built-in responder (PR #3738) answers UMP Stream Discovery from
// those; the app no longer installs a stream responder.

// Subscribable property value. Updated every cycle so subscribers see
// PE Notify deltas.
static char g_overlay_rate[32] = "{\"rateHz\":50}";


/*--------------------------------------------------------------------+
 * MIDI-CI bootstrap: profile + 3 properties + process inquiry
 *--------------------------------------------------------------------*/
static void install_ci_bootstrap(m2ci& ci) {
    // Profile
    ci.addProfile(kProfileId, /*alwaysOn*/ false);
    ci.onProfileEnable([](const uint8_t[5], uint8_t) {});
    ci.onProfileDisable([](const uint8_t[5], uint8_t) {});

    // Property Exchange
    /* App-supplied ResourceList (overrides the lib built-in) so the
     * custom X-OverlayRate entry carries its schema (M2-105). */
    ci.addPropertyStatic("ResourceList",
        "[{\"resource\":\"DeviceInfo\"},"
         "{\"resource\":\"ChannelList\"},"
         "{\"resource\":\"ProgramList\"},"
         "{\"resource\":\"X-OverlayRate\",\"schema\":"
         "{\"title\":\"Overlay Rate\",\"type\":\"object\",\"properties\":"
         "{\"rateHz\":{\"title\":\"Rate (Hz)\",\"type\":\"integer\"}}}}]");

    ci.addPropertyStatic("DeviceInfo",
        "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[9,0],\"versionId\":[0,0,4,0],\"manufacturer\":\"midi2.diy\","
         "\"family\":\"STM32F411\","
         "\"model\":\"WeAct BlackPill F411 MIDI 2.0\","
         "\"version\":\"0.0.1\"}");

    ci.addProperty("ChannelList",
        []() -> const char* { return "[{\"title\":\"Channel 1\",\"channel\":1},{\"title\":\"Channel 2\",\"channel\":2},{\"title\":\"Channel 3\",\"channel\":3},{\"title\":\"Channel 4\",\"channel\":4}]"; },
        nullptr  // read-only
    );

    ci.addPropertyStatic("ProgramList", "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]");

    ci.addProperty("X-OverlayRate",
        []() -> const char* { return g_overlay_rate; },
        [](const char* value) -> bool {
            std::strncpy(g_overlay_rate, value, sizeof(g_overlay_rate) - 1);
            g_overlay_rate[sizeof(g_overlay_rate) - 1] = '\0';
            return true;
        });
    ci.setPropertySubscribable("X-OverlayRate", true);

    // Process Inquiry, advertise capability across all 16 channels.
    ci.setMidiReport(/*msg_data_control*/ 0x01,
                     /*system bitmap*/    0x00000000FFFFFFFFull,
                     /*channel bitmap*/   0xFFFFFFFFFFFFFFFFull,
                     /*note bitmap*/      0xFFFFFFFFFFFFFFFFull);
}

/*--------------------------------------------------------------------+
 * Showcase cycle constants + state
 *--------------------------------------------------------------------*/
constexpr uint8_t  kCh = 0;

// Scene A: Flex Data suite + Start of Clip
constexpr uint32_t kA_StartMs   =     0;

// Scene B: Per-Note expression stack on note 60 (C4)
constexpr uint8_t  kB_Note      =    60;
constexpr uint32_t kB_NoteOnMs  =   400;
constexpr uint32_t kB_NoteOffMs =  6500;
constexpr uint32_t kB_VibUpdMs  =    20;     // 50 Hz Per-Note PB updates
constexpr uint32_t kB_RegPncMs  =  1500;     // Registered Per-Note Controller (volume)
constexpr uint32_t kB_AsnPncMs  =  3000;     // Assignable Per-Note Controller (brightness)
constexpr uint32_t kB_MgmtMs    =  6200;     // Per-Note Management Reset before NoteOff

// Per-Note Pitch Bend vibrato: 5 Hz, +/- ~half a semitone around centre.
constexpr int32_t  kVibratoAmp  = 0x10000000;
constexpr uint32_t kVibratoPerMs =   200;    // 1000 / 5 Hz

// Scene C: chromatic walk (resolution showcase)
constexpr uint8_t  kC_BaseNote  =    72;     // C5
constexpr uint8_t  kC_Count     =     8;
constexpr uint32_t kC_StartMs   =  7000;
constexpr uint32_t kC_StepMs    =   500;
constexpr uint32_t kC_EndMs     =  kC_StartMs + kC_Count * kC_StepMs;  // 11000

// Scene D: Program Change with Bank
constexpr uint32_t kD_Ms        = 11500;

// Scene E: RPN / NRPN / Relative
constexpr uint32_t kE_RpnMs     = 12500;
constexpr uint32_t kE_NrpnMs    = 13000;
constexpr uint32_t kE_RelRpnMs  = 13500;
constexpr uint32_t kE_RelNrpnMs = 14000;

// Scene F: Note with Attribute pitch_7_9
constexpr uint8_t  kF_Note      =    64;     // E4
constexpr uint32_t kF_OnMs      = 14800;
constexpr uint32_t kF_OffMs     = 16500;

// Scene G: SysEx7
constexpr uint32_t kG_Ms        = 17200;

// Scene H: Delta Clockstamp
constexpr uint32_t kH_Ms        = 17700;

// Scene I: Property Exchange Notify
constexpr uint32_t kI_Ms        = 18500;

// Scene J: End of Clip
constexpr uint32_t kJ_Ms        = 19500;

constexpr uint32_t kCycleMs     = 22000;

struct Showcase {
    uint32_t cycle_start_ms = 0;

    bool a_done = false;

    bool b_on        = false;
    bool b_off       = false;
    uint32_t b_last_vib = 0;
    bool b_reg_pnc   = false;
    bool b_asn_pnc   = false;
    bool b_mgmt      = false;

    uint8_t  c_idx        = 0;
    uint32_t c_last_ms    = 0;
    bool     c_released   = false;

    bool d_done = false;
    bool e_rpn_done = false;
    bool e_nrpn_done = false;
    bool e_relrpn_done = false;
    bool e_relnrpn_done = false;

    bool f_on  = false;
    bool f_off = false;
    bool g_done = false;
    bool h_done = false;
    bool i_done = false;
    bool j_done = false;

    uint32_t cycle_count = 0;
};

// Integer triangle-wave approximation of a 5 Hz sine, scaled to
// +/- kVibratoAmp. Integer-only so the showcase never depends on FPU
// enable state at cold boot.
static int32_t vibrato_offset(uint32_t elapsed_ms) {
    uint32_t t = elapsed_ms % kVibratoPerMs;
    if (t < kVibratoPerMs / 2) {
        return (int32_t)((int32_t)t * 4 * kVibratoAmp / (int32_t)kVibratoPerMs - kVibratoAmp);
    }
    return (int32_t)(3 * kVibratoAmp - (int32_t)t * 4 * kVibratoAmp / (int32_t)kVibratoPerMs);
}

/*--------------------------------------------------------------------+
 * Showcase scene runners
 *--------------------------------------------------------------------*/
static void scene_a_flex(m2device& midi, Showcase& s) {
    if (s.a_done) return;
    // Flex Data suite, UMP MT 0xD (MIDI 2.0 only).
    midi.sendTempo(0, /*ten_ns_per_quarter*/ 50000000u);    // 120 BPM
    midi.sendTimeSignature(0, /*num*/ 4, /*denom*/ 2);       // 4/4
    midi.sendKeySignature(0, /*sharps_flats*/ 0, /*minor*/ false);  // C major
    midi.sendMetronome(0, /*primary*/ 24, /*acc1*/ 0, /*acc2*/ 0,
                       /*acc3*/ 0, /*sub1*/ 0, /*sub2*/ 0);
    // Chord encoding per M2-104 §7.5 + Table 14:
    //   address:    01=group-level, 10=channel-level (00/11 reserved)
    //   tonic_note: 0=unknown, 1=A, 2=B, 3=C, 4=D, 5=E, 6=F, 7=G
    //   chord_type: 0x01=Major, 0x03=Major 7, 0x07=Minor, ...
    ChordDescriptor chord{};
    chord.address        = 1;       // group-level (channel field ignored)
    chord.channel        = 0;
    chord.tonicSharpFlat = 0;       // natural
    chord.tonicNote      = 3;       // C
    chord.chordType      = 0x03;    // Major 7
    midi.sendChordName(0, chord);
    midi.sendStartOfClip();
    s.a_done = true;
}

static void scene_b_per_note(m2device& midi, Showcase& s, uint32_t t, uint32_t now) {
    if (!s.b_on && t >= kB_NoteOnMs) {
        midi.noteOn(kCh, kB_Note, 0xC000);
        s.b_on = true;
        s.b_last_vib = 0;
    }
    if (s.b_on && !s.b_off && t < kB_NoteOffMs) {
        // Per-Note Pitch Bend at 50 Hz
        if (now - s.b_last_vib >= kB_VibUpdMs) {
            int32_t off = vibrato_offset(t - kB_NoteOnMs);
            uint32_t pb = (uint32_t)((int64_t)0x80000000 + off);
            midi.sendPerNotePitchBend(0, kCh, kB_Note, pb);
            s.b_last_vib = now;
        }
    }
    if (!s.b_reg_pnc && s.b_on && t >= kB_RegPncMs) {
        // Registered Per-Note Controller #7 (Volume), UNIQUE to MIDI 2.0.
        midi.sendRegPerNoteController(0, kCh, kB_Note, /*idx*/ 7,
                                      /*val32*/ 0xC0000000u);
        s.b_reg_pnc = true;
    }
    if (!s.b_asn_pnc && s.b_on && t >= kB_AsnPncMs) {
        // Assignable Per-Note Controller #74 (filter / brightness).
        midi.sendAsnPerNoteController(0, kCh, kB_Note, /*idx*/ 74,
                                      /*val32*/ 0xA0000000u);
        s.b_asn_pnc = true;
    }
    if (!s.b_mgmt && s.b_on && t >= kB_MgmtMs) {
        // Per-Note Management Reset (clears per-note controllers without
        // ending the note).
        midi.sendPerNoteManagement(0, kCh, kB_Note, /*detach*/ false, /*reset*/ true);
        s.b_mgmt = true;
    }
    if (s.b_on && !s.b_off && t >= kB_NoteOffMs) {
        midi.sendPerNotePitchBend(0, kCh, kB_Note, 0x80000000u);
        midi.noteOff(kCh, kB_Note);
        s.b_off = true;
    }
}

static void scene_c_walk(m2device& midi, Showcase& s, uint32_t t, uint32_t now) {
    if (s.c_idx < kC_Count && t >= kC_StartMs &&
        (s.c_idx == 0 || (now - s.c_last_ms) >= kC_StepMs)) {

        uint16_t vel = (uint16_t)(0x2000u + (uint32_t)s.c_idx *
                                  ((0xFFFFu - 0x2000u) / (kC_Count - 1)));
        if (s.c_idx > 0) {
            midi.noteOff(kCh, (uint8_t)(kC_BaseNote + s.c_idx - 1));
        }
        uint8_t note = (uint8_t)(kC_BaseNote + s.c_idx);
        midi.noteOn(kCh, note, vel);

        // 32-bit CC sweep on CC #74
        uint32_t cc_val = 0x20000000u + (uint32_t)s.c_idx *
                          ((0xFFFFFFFFu - 0x20000000u) / (kC_Count - 1));
        midi.cc(kCh, /*idx*/ 74, cc_val);

        // 32-bit channel Pitch Bend ramp (centre to +max across walk)
        uint32_t pb = 0x80000000u + (uint32_t)s.c_idx *
                      ((0xFFFFFFFFu - 0x80000000u) / (kC_Count - 1));
        midi.sendPitchBend(0, kCh, pb);

        // 32-bit Poly Pressure
        uint32_t poly = (uint32_t)s.c_idx * (0xFFFFFFFFu / (kC_Count - 1));
        midi.sendPolyPressure(0, kCh, note, poly);

        // 32-bit Channel Pressure
        uint32_t chp = 0x40000000u + (uint32_t)s.c_idx *
                       ((0xC0000000u) / (kC_Count - 1));
        midi.sendChannelPressure(0, kCh, chp);

        s.c_last_ms = now;
        s.c_idx++;
    }
    if (!s.c_released && s.c_idx == kC_Count && t >= kC_EndMs) {
        midi.noteOff(kCh, (uint8_t)(kC_BaseNote + kC_Count - 1));
        // Reset channel-wide controllers we ramped.
        midi.sendPitchBend(0, kCh, 0x80000000u);
        midi.sendChannelPressure(0, kCh, 0u);
        s.c_released = true;
    }
}

static void scene_d_program(m2device& midi, Showcase& s, uint32_t t) {
    if (s.d_done || t < kD_Ms) return;
    // Program Change with Bank in a single UMP, MIDI 1.0 needs 3 messages
    // (BankMSB CC#0, BankLSB CC#32, Program).
    midi.sendProgram(0, kCh, /*program*/ 42,
                     /*bankMSB*/ 0x10, /*bankLSB*/ 0x05, /*bankValid*/ true);
    s.d_done = true;
}

static void scene_e_rpn_nrpn(m2device& midi, Showcase& s, uint32_t t) {
    if (!s.e_rpn_done && t >= kE_RpnMs) {
        // RPN 0/0 = Pitch Bend Sensitivity. In MIDI 2.0 it carries a
        // 32-bit value with full resolution.
        midi.sendRpn(0, kCh, /*msb*/ 0, /*lsb*/ 0, /*val32*/ 0x40000000u);
        s.e_rpn_done = true;
    }
    if (!s.e_nrpn_done && t >= kE_NrpnMs) {
        midi.sendNrpn(0, kCh, /*msb*/ 0x12, /*lsb*/ 0x34, /*val32*/ 0xDEADBEEFu);
        s.e_nrpn_done = true;
    }
    if (!s.e_relrpn_done && t >= kE_RelRpnMs) {
        // Relative RPN = increment without round-trip read. UNIQUE to 2.0.
        midi.sendRelRpn(0, kCh, /*msb*/ 0, /*lsb*/ 0, /*delta*/ 0x01000000);
        s.e_relrpn_done = true;
    }
    if (!s.e_relnrpn_done && t >= kE_RelNrpnMs) {
        midi.sendRelNrpn(0, kCh, /*msb*/ 0x12, /*lsb*/ 0x34, /*delta*/ -0x00800000);
        s.e_relnrpn_done = true;
    }
}

static void scene_f_attribute(m2device& midi, Showcase& s, uint32_t t) {
    if (!s.f_on && t >= kF_OnMs) {
        // attribute_type 0x03 = pitch_7_9. The 16-bit attribute_data is
        // [7-bit note number][9-bit fractional]. +50 cents = +0.5
        // semitone = (1 << 9) >> 1 = 256 in the fractional field.
        uint16_t attr_data = (uint16_t)(((uint16_t)kF_Note << 9) | 256);
        midi.sendNoteOn(0, kCh, kF_Note, /*vel16*/ 0xC000,
                        /*attrType*/ 0x03, /*attrData*/ attr_data);
        s.f_on = true;
    }
    if (!s.f_off && t >= kF_OffMs) {
        midi.sendNoteOff(0, kCh, kF_Note, /*vel16*/ 0,
                         /*attrType*/ 0x03, /*attrData*/ 0);
        s.f_off = true;
    }
}

static void scene_g_sysex7(m2device& midi, Showcase& s, uint32_t t) {
    if (s.g_done || t < kG_Ms) return;
    // Universal SysEx Identity Reply, 12 bytes of 7-bit data.
    // sendSysEx7 fragments automatically into Start + End packets (MT 0x3).
    static const uint8_t payload[] = {
        0x7E, 0x7F, 0x06, 0x02,  // Universal Non-Realtime, device 0x7F, Identity Reply
        0x7D, 0x01, 0x00, 0x40,  // mfr id + family LSB/MSB + model LSB
        0x00, 0x04, 0x00, 0x00,  // model MSB + version v0.4.0.0
    };
    midi.sendSysEx7(/*group*/ 0, payload, sizeof(payload));
    s.g_done = true;
}

static void scene_h_dctpq(m2device& midi, Showcase& s, uint32_t t) {
    if (s.h_done || t < kH_Ms) return;
    // Delta Clockstamp Ticks Per Quarter Note + a delta tick value.
    // Both are MIDI 2.0 utility messages (MT 0x0).
    midi.sendDctpq(/*tpq*/ 480);
    midi.sendDeltaClockstamp(/*ticks*/ 240);  // half a beat at 480 TPQ
    s.h_done = true;
}

static void scene_i_pe_notify(m2ci& ci, Showcase& s, uint32_t t) {
    if (s.i_done || t < kI_Ms) return;
    // Update the subscribable property and broadcast Notify to any active
    // subscribers. With no subscribers this is a no-op on the wire, but
    // the API path still runs.
    std::snprintf(g_overlay_rate, sizeof(g_overlay_rate),
                  "{\"rateHz\":%u}", (unsigned)(50 + s.cycle_count));
    ci.notifyPropertyChanged("X-OverlayRate");
    s.i_done = true;
}

static void scene_j_clip_end(m2device& midi, Showcase& s, uint32_t t) {
    if (s.j_done || t < kJ_Ms) return;
    midi.sendEndOfClip();
    s.j_done = true;
}

/*--------------------------------------------------------------------+
 * Top-level cycle driver
 *--------------------------------------------------------------------*/
static void showcase_step(m2device& midi, m2ci& ci, Showcase& s) {
    if (!midi.isMounted() || midi.altSetting() != 1) return;

    uint32_t now = (uint32_t)tusb_time_millis_api();

    if (s.cycle_start_ms == 0 || (now - s.cycle_start_ms) >= kCycleMs) {
        uint32_t prev_count = s.cycle_count;
        s = Showcase{};
        s.cycle_start_ms = now;
        s.cycle_count = prev_count + 1;
    }
    uint32_t t = now - s.cycle_start_ms;

    scene_a_flex(midi, s);
    scene_b_per_note(midi, s, t, now);
    scene_c_walk(midi, s, t, now);
    scene_d_program(midi, s, t);
    scene_e_rpn_nrpn(midi, s, t);
    scene_f_attribute(midi, s, t);
    scene_g_sysex7(midi, s, t);
    scene_h_dctpq(midi, s, t);
    scene_i_pe_notify(ci, s, t);
    scene_j_clip_end(midi, s, t);
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

    install_ci_bootstrap(ci);

    static Showcase showcase{};
    bool prev_mounted = false;

    while (true) {
        midi2_board::task(midi);

        bool mounted = midi.isMounted();
        if (mounted != prev_mounted) {
            midi2_board::led_show_mounted(mounted);
            prev_mounted = mounted;
        }

        showcase_step(midi, ci, showcase);
    }
}

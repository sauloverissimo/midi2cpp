/*
 * waveshare-rp2040-midi2-showcase: full-surface USB MIDI 2.0 device showcase, ~22 s scene cycle
 * while mounted.
 *
 * Boot: UMP Stream Discovery responder (TinyUSB #3738 built-in) + MIDI-CI
 * responder: Discovery, Profile GM 1, PE (DeviceInfo, ChannelList,
 * ProgramList, X-OverlayRate subscribable), Process Inquiry MIDI Report.
 *
 * Scenes:
 *   A  Flex Data: Tempo, TimeSig, KeySig, Metronome, Chord + Start of Clip
 *   B  Per-Note: PB vibrato, Registered/Assignable controllers, Mgmt Reset
 *   C  Resolution walk: 16-bit velocity, 32-bit CC / PB / PolyP / ChanP
 *   D  Program Change with Bank in a single UMP
 *   E  RPN/NRPN 32-bit + Relative RPN/NRPN
 *   F  Note On attribute pitch_7_9 (+50 cents)
 *   G  SysEx7 (fragmented Universal Identity Reply)
 *   H  Delta Clockstamp (DCTPQ + delta ticks)
 *   I  PE Notify: X-OverlayRate broadcast to subscribers
 *   J  End of Clip
 * Always: JR Timestamp heartbeat (500 ms).
 *
 * each scene logs to UART and emits observable UMPs.
 * UART debug print on GP0 / GP1 @ 115200 8N1. LED follows mounted state.
 */
#include <cstdio>
#include <cstring>
#include <cmath>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "bsp/board_api.h"

#include "board_midi2.h"

using namespace midi2;

/*--------------------------------------------------------------------+
 * Identity
 *--------------------------------------------------------------------*/
static const uint8_t  kMfrId[3]      = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId      = 0x0001;
static const uint16_t kModelId       = 0x0005;
static const uint32_t kVersion       = 0x00010000;
static const uint8_t  kProfileId[5]     = {0x7E, 0x00, 0x00, 0x01, 0x00};

// Subscribable property value. Updated every cycle so subscribers see
// PE Notify deltas.
static char g_overlay_rate[32] = "{\"rateHz\":50}";

/*--------------------------------------------------------------------+
 * UMP Stream Discovery is answered by the TinyUSB built-in responder
 * (PR #3738): Endpoint Name from CFG_TUD_MIDI2_EP_NAME, FB direction +
 * group span from tud_midi2_gtb_desc_cb, FB name from tud_midi2_fb_name_cb
 * (in the board glue). No app-side stream responder is installed; Device
 * Identity is carried by MIDI-CI Discovery (SysEx) via ci.begin.
 *--------------------------------------------------------------------*/

/*--------------------------------------------------------------------+
 * MIDI-CI bootstrap: profile + 3 properties + process inquiry
 *--------------------------------------------------------------------*/
static void install_ci_bootstrap(m2ci& ci) {
    // Profile
    int rc = ci.addProfile(kProfileId, /*alwaysOn*/ false);
    std::printf("[ci] addProfile rc=%d\r\n", rc);

    ci.onProfileEnable([](const uint8_t id[5], uint8_t numChannels) {
        std::printf("[ci] Profile ENABLE  %02X %02X %02X %02X %02X (n=%u)\r\n",
                    id[0], id[1], id[2], id[3], id[4], (unsigned)numChannels);
    });
    ci.onProfileDisable([](const uint8_t id[5], uint8_t numChannels) {
        std::printf("[ci] Profile DISABLE %02X %02X %02X %02X %02X (n=%u)\r\n",
                    id[0], id[1], id[2], id[3], id[4], (unsigned)numChannels);
    });

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

    rc = ci.addPropertyStatic("DeviceInfo",
        "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[5,0],\"versionId\":[0,0,4,0],\"manufacturer\":\"midi2.diy\","
         "\"family\":\"RP2040\","
         "\"model\":\"Waveshare RP2040 Pi Zero MIDI 2.0\","
         "\"version\":\"0.0.1\"}");
    std::printf("[ci] addPropertyStatic(DeviceInfo) rc=%d\r\n", rc);

    rc = ci.addProperty("ChannelList",
        []() -> const char* { return "[{\"title\":\"Channel 1\",\"channel\":1},{\"title\":\"Channel 2\",\"channel\":2},{\"title\":\"Channel 3\",\"channel\":3},{\"title\":\"Channel 4\",\"channel\":4}]"; },
        nullptr  // read-only
    );
    std::printf("[ci] addProperty(ChannelList) rc=%d\r\n", rc);

    rc = ci.addPropertyStatic("ProgramList", "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]");
    std::printf("[ci] addPropertyStatic(ProgramList) rc=%d\r\n", rc);

    rc = ci.addProperty("X-OverlayRate",
        []() -> const char* { return g_overlay_rate; },
        [](const char* value) -> bool {
            std::strncpy(g_overlay_rate, value, sizeof(g_overlay_rate) - 1);
            g_overlay_rate[sizeof(g_overlay_rate) - 1] = '\0';
            std::printf("[ci] X-OverlayRate set to %s\r\n", g_overlay_rate);
            return true;
        });
    std::printf("[ci] addProperty(X-OverlayRate) rc=%d\r\n", rc);
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
    // Tempo / TimeSig / KeySig / Metronome all use group-level address by
    // default; we mirror that here so the chord applies to the whole group.
    ChordDescriptor chord{};
    chord.address        = 1;       // group-level (channel field ignored)
    chord.channel        = 0;
    chord.tonicSharpFlat = 0;       // natural
    chord.tonicNote      = 3;       // C
    chord.chordType      = 0x03;    // Major 7
    midi.sendChordName(0, chord);
    midi.sendStartOfClip();
    std::printf("[A] Flex Data suite + Start of Clip\r\n");
    s.a_done = true;
}

static void scene_b_per_note(m2device& midi, Showcase& s, uint32_t t, uint32_t now) {
    if (!s.b_on && t >= kB_NoteOnMs) {
        midi.noteOn(kCh, kB_Note, 0xC000);
        s.b_on = true;
        s.b_last_vib = 0;
        std::printf("[B] note on C4, Per-Note PB vibrato + Per-Note CCs incoming\r\n");
    }
    if (s.b_on && !s.b_off && t < kB_NoteOffMs) {
        // Per-Note Pitch Bend at 50 Hz
        if (now - s.b_last_vib >= kB_VibUpdMs) {
            float secs = (float)(t - kB_NoteOnMs) / 1000.0f;
            float v    = sinf(secs * 2.0f * 3.14159265f * 5.0f);
            int32_t off = (int32_t)(v * (float)0x10000000);
            uint32_t pb = (uint32_t)((int64_t)0x80000000 + off);
            midi.sendPerNotePitchBend(0, kCh, kB_Note, pb);
            s.b_last_vib = now;
        }
    }
    if (!s.b_reg_pnc && s.b_on && t >= kB_RegPncMs) {
        // Registered Per-Note Controller #7 (Volume), UNIQUE to MIDI 2.0.
        midi.sendRegPerNoteController(0, kCh, kB_Note, /*idx*/ 7,
                                      /*val32*/ 0xC0000000u);
        std::printf("[B] Registered Per-Note Controller #7 (volume) val=0xC0000000\r\n");
        s.b_reg_pnc = true;
    }
    if (!s.b_asn_pnc && s.b_on && t >= kB_AsnPncMs) {
        // Assignable Per-Note Controller #74 (filter / brightness).
        midi.sendAsnPerNoteController(0, kCh, kB_Note, /*idx*/ 74,
                                      /*val32*/ 0xA0000000u);
        std::printf("[B] Assignable Per-Note Controller #74 (brightness) val=0xA0000000\r\n");
        s.b_asn_pnc = true;
    }
    if (!s.b_mgmt && s.b_on && t >= kB_MgmtMs) {
        // Per-Note Management Reset (clears per-note controllers without
        // ending the note).
        midi.sendPerNoteManagement(0, kCh, kB_Note, /*detach*/ false, /*reset*/ true);
        std::printf("[B] Per-Note Management Reset\r\n");
        s.b_mgmt = true;
    }
    if (s.b_on && !s.b_off && t >= kB_NoteOffMs) {
        midi.sendPerNotePitchBend(0, kCh, kB_Note, 0x80000000u);
        midi.noteOff(kCh, kB_Note);
        s.b_off = true;
        std::printf("[B] note off\r\n");
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

        // 32-bit channel Pitch Bend ramp (centre → +max across walk)
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

        std::printf("[C] step %u note=%u vel=0x%04X cc74=0x%08X pb=0x%08X poly=0x%08X chp=0x%08X\r\n",
                    (unsigned)s.c_idx, (unsigned)note, (unsigned)vel,
                    (unsigned)cc_val, (unsigned)pb, (unsigned)poly, (unsigned)chp);

        s.c_last_ms = now;
        s.c_idx++;
    }
    if (!s.c_released && s.c_idx == kC_Count && t >= kC_EndMs) {
        midi.noteOff(kCh, (uint8_t)(kC_BaseNote + kC_Count - 1));
        // Reset channel-wide controllers we ramped.
        midi.sendPitchBend(0, kCh, 0x80000000u);
        midi.sendChannelPressure(0, kCh, 0u);
        s.c_released = true;
        std::printf("[C] walk end (PB/ChP reset)\r\n");
    }
}

static void scene_d_program(m2device& midi, Showcase& s, uint32_t t) {
    if (s.d_done || t < kD_Ms) return;
    // Program Change with Bank in a single UMP, MIDI 1.0 needs 3 messages
    // (BankMSB CC#0, BankLSB CC#32, Program).
    midi.sendProgram(0, kCh, /*program*/ 42,
                     /*bankMSB*/ 0x10, /*bankLSB*/ 0x05, /*bankValid*/ true);
    std::printf("[D] Program=42 with Bank MSB=0x10 LSB=0x05 (single UMP)\r\n");
    s.d_done = true;
}

static void scene_e_rpn_nrpn(m2device& midi, Showcase& s, uint32_t t) {
    if (!s.e_rpn_done && t >= kE_RpnMs) {
        // RPN 0/0 = Pitch Bend Sensitivity. Spec value is "semitones"
        // in MSB and "cents" in LSB on MIDI 1.0; in MIDI 2.0 it's a
        // 32-bit value with full resolution.
        midi.sendRpn(0, kCh, /*msb*/ 0, /*lsb*/ 0, /*val32*/ 0x40000000u);
        std::printf("[E] RPN 0/0 (Pitch Bend Sensitivity) val=0x40000000\r\n");
        s.e_rpn_done = true;
    }
    if (!s.e_nrpn_done && t >= kE_NrpnMs) {
        midi.sendNrpn(0, kCh, /*msb*/ 0x12, /*lsb*/ 0x34, /*val32*/ 0xDEADBEEFu);
        std::printf("[E] NRPN 0x12/0x34 val=0xDEADBEEF\r\n");
        s.e_nrpn_done = true;
    }
    if (!s.e_relrpn_done && t >= kE_RelRpnMs) {
        // Relative RPN = increment without round-trip read. UNIQUE to 2.0.
        midi.sendRelRpn(0, kCh, /*msb*/ 0, /*lsb*/ 0, /*delta*/ 0x01000000);
        std::printf("[E] Relative RPN 0/0 delta=+0x01000000\r\n");
        s.e_relrpn_done = true;
    }
    if (!s.e_relnrpn_done && t >= kE_RelNrpnMs) {
        midi.sendRelNrpn(0, kCh, /*msb*/ 0x12, /*lsb*/ 0x34, /*delta*/ -0x00800000);
        std::printf("[E] Relative NRPN 0x12/0x34 delta=-0x00800000\r\n");
        s.e_relnrpn_done = true;
    }
}

static void scene_f_attribute(m2device& midi, Showcase& s, uint32_t t) {
    if (!s.f_on && t >= kF_OnMs) {
        // attribute_type 0x03 = pitch_7_9. The 16-bit attribute_data is
        // [7-bit note number][9-bit fractional]. Centre = note as-is.
        // +50 cents = +0.5 semitone = (1 << 9) >> 1 = 256 in the
        // fractional field.
        uint16_t attr_data = (uint16_t)(((uint16_t)kF_Note << 9) | 256);
        midi.sendNoteOn(0, kCh, kF_Note, /*vel16*/ 0xC000,
                        /*attrType*/ 0x03, /*attrData*/ attr_data);
        std::printf("[F] Note On with Attribute pitch_7_9 (E4 +50¢) attr_data=0x%04X\r\n",
                    (unsigned)attr_data);
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
    std::printf("[G] SysEx7 emitted (%zu bytes, Identity Reply)\r\n", sizeof(payload));
    s.g_done = true;
}

static void scene_h_dctpq(m2device& midi, Showcase& s, uint32_t t) {
    if (s.h_done || t < kH_Ms) return;
    // Delta Clockstamp Ticks Per Quarter Note + a delta tick value.
    // Both are MIDI 2.0 utility messages (MT 0x0).
    midi.sendDctpq(/*tpq*/ 480);
    midi.sendDeltaClockstamp(/*ticks*/ 240);  // half a beat at 480 TPQ
    std::printf("[H] DCTPQ=480 + Delta Clockstamp=240 ticks\r\n");
    s.h_done = true;
}

static void scene_i_pe_notify(m2ci& ci, Showcase& s, uint32_t t) {
    if (s.i_done || t < kI_Ms) return;
    // Update the subscribable property and broadcast Notify to any
    // active subscribers. If there are no subscribers, this is a no-op
    // on the wire, but the cycle counter logged still proves the API
    // path works.
    std::snprintf(g_overlay_rate, sizeof(g_overlay_rate),
                  "{\"rateHz\":%u}", (unsigned)(50 + s.cycle_count));
    ci.notifyPropertyChanged("X-OverlayRate");
    std::printf("[I] PE Notify OverlayRate=%s subscribers=%u\r\n",
                g_overlay_rate, (unsigned)ci.subscriberCount());
    s.i_done = true;
}

static void scene_j_clip_end(m2device& midi, Showcase& s, uint32_t t) {
    if (s.j_done || t < kJ_Ms) return;
    midi.sendEndOfClip();
    std::printf("[J] End of Clip\r\n");
    s.j_done = true;
}

/*--------------------------------------------------------------------+
 * Top-level cycle driver
 *--------------------------------------------------------------------*/
static void showcase_step(m2device& midi, m2ci& ci, Showcase& s) {
    if (!midi.isMounted() || midi.altSetting() != 1) return;

    uint32_t now = (uint32_t)(time_us_64() / 1000ULL);

    if (s.cycle_start_ms == 0 || (now - s.cycle_start_ms) >= kCycleMs) {
        uint32_t prev_count = s.cycle_count;
        s = Showcase{};
        s.cycle_start_ms = now;
        s.cycle_count = prev_count + 1;
        std::printf("\r\n[cycle %u] start at %u ms\r\n",
                    (unsigned)s.cycle_count, (unsigned)now);
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
    stdio_init_all();
    sleep_ms(200);

    std::printf("\r\n===========================================\r\n");
    std::printf("  waveshare-rp2040-midi2-showcase  (VID:PID 0xCAFE:0x4072)\r\n");
    std::printf("===========================================\r\n");

    m2device midi;
    m2ci     ci(midi);

    midi2_board::init(midi, ci);
    midi.begin();
    midi.enableJRHeartbeat(500);
    ci.begin(kMfrId, kFamilyId, kModelId, kVersion);

    install_ci_bootstrap(ci);

    midi.onNoteOn([](uint8_t ch, uint8_t note, uint16_t vel) {
        std::printf("[in] NoteOn ch=%u note=%u vel=%u\r\n",
                    (unsigned)ch, (unsigned)note, (unsigned)vel);
    });

    std::printf("[ready] entering main loop\r\n");

    Showcase showcase{};
    while (true) {
        midi2_board::task(midi);
        board_led_write(midi.isMounted());
        showcase_step(midi, ci, showcase);
    }
}

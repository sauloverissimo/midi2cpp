/*
 * main.cpp, esp32-s3-devkitc-usb-midi2-showcase
 *
 * Headless full-spec USB MIDI 2.0 device showcase on the ESP32-S3
 * DevKitC-1 native USB-OTG port. Mirrors the rp2040-midi2 22 s scene
 * cycle, exercising every category of message MIDI 2.0 brings beyond
 * MIDI 1.0. The features are ordered to be inspectable: each scene
 * logs to UART (USB-to-UART bridge, left jack) and emits observable
 * UMPs on the native USB-OTG (right jack).
 *
 *   Boot (once):
 *     - UMP Stream Discovery responder (Endpoint Info, Device Identity,
 *       Endpoint Name, Product Instance ID, Stream Config, FB Info,
 *       FB Name)
 *     - MIDI-CI Discovery + PE Capability + PE Get auto-replied via
 *       m2ci's Appendix E convenience responder
 *     - 1 Custom Profile registered (id 7D 00 00 01 00)
 *     - 3 Properties: static DeviceInfo, dynamic ChannelList,
 *       subscribable OverlayRate (broadcast to subscribers each cycle)
 *     - Process Inquiry: setMidiReport (system + channel + note bitmaps)
 *
 *   Each cycle (~22 s):
 *     Scene A, Flex Data suite: Tempo, Time Sig, Key Sig, Metronome,
 *              Chord Name (Cmaj7) + Start of Clip
 *     Scene B, Per-Note expression stack on a single sustained note:
 *              Per-Note Pitch Bend vibrato + Registered Per-Note
 *              Controller (volume) + Assignable Per-Note Controller
 *              (brightness) + Per-Note Management Reset at end
 *     Scene C, Resolution showcase (chromatic walk): 16-bit variable
 *              velocity + 32-bit CC sweep + 32-bit Pitch Bend ramp +
 *              32-bit Poly Pressure + 32-bit Channel Pressure
 *     Scene D, Program Change with Bank in a single UMP
 *     Scene E, RPN/NRPN 32-bit + Relative RPN/NRPN (incremental)
 *     Scene F, Note On with Attribute pitch_7_9 (microtonal +50¢)
 *     Scene G, SysEx8 emission (8-bit SysEx, no 7-bit aliasing)
 *     Scene H, Delta Clockstamp (DCTPQ + delta ticks)
 *     Scene I, Property Exchange Notify (broadcast OverlayRate change
 *              to any current subscribers)
 *     Scene J, End of Clip
 *
 *   Always:
 *     - JR Timestamp heartbeat (500 ms, also a MIDI 2.0-only message)
 *     - On-board RGB LED (GPIO48): green when mounted, red otherwise
 *
 * UART debug print on the USB-to-UART bridge (left jack) at 115200
 * 8N1.
 */
#include <cstdio>
#include <cstring>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp32_s3_devkitc_midi2.h"

using namespace midi2;

/*--------------------------------------------------------------------+
 * Identity
 *--------------------------------------------------------------------*/
static const uint8_t  kMfrId[3]        = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId        = 0x0001;
static const uint16_t kModelId         = 0x0001;
static const uint32_t kVersion         = 0x00010000;
static const char     kEndpointName[]  = "ESP32S3DevKitC";
static const char     kProductInstId[] = "ESP32S3DevKitC-showcase-0001";
static const char     kFbName[]        = "Main";
static const uint8_t  kProfileId[5]    = {0x7D, 0x00, 0x00, 0x01, 0x00};

// Subscribable property value. Updated every cycle so subscribers see
// PE Notify deltas.
static char g_overlay_rate[32] = "{\"rateHz\":50}";

/*--------------------------------------------------------------------+
 * UMP Stream responder
 *--------------------------------------------------------------------*/
static void install_stream_responder(m2device& midi) {
    midi.onEndpointDiscovery([&midi](uint8_t filter) {
        std::printf("[stream] Endpoint Discovery filter=0x%02X\r\n", filter);
        if (filter & 0x01) {
            midi.sendEndpointInfo(/*ump_ver*/ 1, 1,
                                  /*static_fb*/ true, /*num_fb*/ 1,
                                  /*midi2*/ true, /*midi1*/ true,
                                  /*rx_jr*/ false, /*tx_jr*/ true);
        }
        if (filter & 0x02) midi.sendDeviceIdentity(kMfrId, kFamilyId, kModelId, kVersion);
        if (filter & 0x04) midi.sendEndpointNameUpdate(kEndpointName);
        if (filter & 0x08) midi.sendProductInstanceIdUpdate(kProductInstId);
        if (filter & 0x10) midi.sendStreamConfigNotify(/*protocol*/ 0x02);
    });
    midi.onFbDiscovery([&midi](uint8_t fbNum, uint8_t filter) {
        std::printf("[stream] FB Discovery fb=%u filter=0x%02X\r\n",
                    (unsigned)fbNum, (unsigned)filter);
        uint8_t target = (fbNum == 0xFF) ? 0 : fbNum;
        if (target != 0) return;
        if (filter & 0x01) {
            midi.sendFbInfo(/*active*/ true, /*fb_num*/ 0,
                            /*direction*/ 0x03, /*ui_hint*/ 0x03,
                            /*first_group*/ 0, /*num_groups*/ 1,
                            /*midi_ci_ver*/ 0x02, /*sysex8*/ false,
                            /*protocol*/ 0x02);
        }
        if (filter & 0x02) midi.sendFbNameUpdate(0, kFbName);
    });
    midi.onStreamConfigRequest([&midi](uint8_t protocol) {
        std::printf("[stream] Config Request protocol=0x%02X\r\n", (unsigned)protocol);
        midi.sendStreamConfigNotify(protocol);
    });
}

/*--------------------------------------------------------------------+
 * MIDI-CI bootstrap: profile + 3 properties + process inquiry
 *--------------------------------------------------------------------*/
static void install_ci_bootstrap(m2ci& ci) {
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

    rc = ci.addPropertyStatic("DeviceInfo",
        "{\"manufacturer\":\"github.com/sauloverissimo\","
         "\"family\":\"esp32-s3-devkitc-usb-midi2\","
         "\"model\":\"showcase\","
         "\"version\":\"0.1.0\"}");
    std::printf("[ci] addPropertyStatic(DeviceInfo) rc=%d\r\n", rc);

    rc = ci.addProperty("ChannelList",
        []() -> const char* { return "{\"channels\":[0,1,2,3]}"; },
        nullptr  // read-only
    );
    std::printf("[ci] addProperty(ChannelList) rc=%d\r\n", rc);

    rc = ci.addProperty("OverlayRate",
        []() -> const char* { return g_overlay_rate; },
        [](const char* value) -> bool {
            std::strncpy(g_overlay_rate, value, sizeof(g_overlay_rate) - 1);
            g_overlay_rate[sizeof(g_overlay_rate) - 1] = '\0';
            std::printf("[ci] OverlayRate set to %s\r\n", g_overlay_rate);
            return true;
        });
    std::printf("[ci] addProperty(OverlayRate) rc=%d\r\n", rc);
    ci.setPropertySubscribable("OverlayRate", true);

    ci.setMidiReport(/*msg_data_control*/ 0x01,
                     /*system bitmap*/    0x00000000FFFFFFFFull,
                     /*channel bitmap*/   0xFFFFFFFFFFFFFFFFull,
                     /*note bitmap*/      0xFFFFFFFFFFFFFFFFull);
}

/*--------------------------------------------------------------------+
 * Showcase cycle constants + state (mirrors rp2040-midi2)
 *--------------------------------------------------------------------*/
constexpr uint8_t  kCh = 0;

constexpr uint32_t kA_StartMs   =     0;

constexpr uint8_t  kB_Note      =    60;
constexpr uint32_t kB_NoteOnMs  =   400;
constexpr uint32_t kB_NoteOffMs =  6500;
constexpr uint32_t kB_VibUpdMs  =    20;
constexpr uint32_t kB_RegPncMs  =  1500;
constexpr uint32_t kB_AsnPncMs  =  3000;
constexpr uint32_t kB_MgmtMs    =  6200;

constexpr uint8_t  kC_BaseNote  =    72;
constexpr uint8_t  kC_Count     =     8;
constexpr uint32_t kC_StartMs   =  7000;
constexpr uint32_t kC_StepMs    =   500;
constexpr uint32_t kC_EndMs     =  kC_StartMs + kC_Count * kC_StepMs;

constexpr uint32_t kD_Ms        = 11500;

constexpr uint32_t kE_RpnMs     = 12500;
constexpr uint32_t kE_NrpnMs    = 13000;
constexpr uint32_t kE_RelRpnMs  = 13500;
constexpr uint32_t kE_RelNrpnMs = 14000;

constexpr uint8_t  kF_Note      =    64;
constexpr uint32_t kF_OnMs      = 14800;
constexpr uint32_t kF_OffMs     = 16500;

constexpr uint32_t kG_Ms        = 17200;
constexpr uint32_t kH_Ms        = 17700;
constexpr uint32_t kI_Ms        = 18500;
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
 * Showcase scene runners (port of rp2040-midi2 main.cpp)
 *--------------------------------------------------------------------*/
static void scene_a_flex(m2device& midi, Showcase& s) {
    if (s.a_done) return;
    midi.sendTempo(0, /*ten_ns_per_quarter*/ 50000000u);
    midi.sendTimeSignature(0, /*num*/ 4, /*denom*/ 2);
    midi.sendKeySignature(0, /*sharps_flats*/ 0, /*minor*/ false);
    midi.sendMetronome(0, /*primary*/ 24, /*acc1*/ 0, /*acc2*/ 0,
                       /*acc3*/ 0, /*sub1*/ 0, /*sub2*/ 0);
    ChordDescriptor chord{};
    chord.address        = 1;
    chord.channel        = 0;
    chord.tonicSharpFlat = 0;
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
        midi.sendRegPerNoteController(0, kCh, kB_Note, /*idx*/ 7,
                                      /*val32*/ 0xC0000000u);
        std::printf("[B] Registered Per-Note Controller #7 (volume) val=0xC0000000\r\n");
        s.b_reg_pnc = true;
    }
    if (!s.b_asn_pnc && s.b_on && t >= kB_AsnPncMs) {
        midi.sendAsnPerNoteController(0, kCh, kB_Note, /*idx*/ 74,
                                      /*val32*/ 0xA0000000u);
        std::printf("[B] Assignable Per-Note Controller #74 (brightness) val=0xA0000000\r\n");
        s.b_asn_pnc = true;
    }
    if (!s.b_mgmt && s.b_on && t >= kB_MgmtMs) {
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

        uint32_t cc_val = 0x20000000u + (uint32_t)s.c_idx *
                          ((0xFFFFFFFFu - 0x20000000u) / (kC_Count - 1));
        midi.cc(kCh, /*idx*/ 74, cc_val);

        uint32_t pb = 0x80000000u + (uint32_t)s.c_idx *
                      ((0xFFFFFFFFu - 0x80000000u) / (kC_Count - 1));
        midi.sendPitchBend(0, kCh, pb);

        uint32_t poly = (uint32_t)s.c_idx * (0xFFFFFFFFu / (kC_Count - 1));
        midi.sendPolyPressure(0, kCh, note, poly);

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
        midi.sendPitchBend(0, kCh, 0x80000000u);
        midi.sendChannelPressure(0, kCh, 0u);
        s.c_released = true;
        std::printf("[C] walk end (PB/ChP reset)\r\n");
    }
}

static void scene_d_program(m2device& midi, Showcase& s, uint32_t t) {
    if (s.d_done || t < kD_Ms) return;
    midi.sendProgram(0, kCh, /*program*/ 42,
                     /*bankMSB*/ 0x10, /*bankLSB*/ 0x05, /*bankValid*/ true);
    std::printf("[D] Program=42 with Bank MSB=0x10 LSB=0x05 (single UMP)\r\n");
    s.d_done = true;
}

static void scene_e_rpn_nrpn(m2device& midi, Showcase& s, uint32_t t) {
    if (!s.e_rpn_done && t >= kE_RpnMs) {
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
        uint16_t attr_data = (uint16_t)(((uint16_t)kF_Note << 9) | 256);
        midi.sendNoteOn(0, kCh, kF_Note, /*vel16*/ 0xC000,
                        /*attrType*/ 0x03, /*attrData*/ attr_data);
        std::printf("[F] Note On with Attribute pitch_7_9 (E4 +50c) attr_data=0x%04X\r\n",
                    (unsigned)attr_data);
        s.f_on = true;
    }
    if (!s.f_off && t >= kF_OffMs) {
        midi.sendNoteOff(0, kCh, kF_Note, /*vel16*/ 0,
                         /*attrType*/ 0x03, /*attrData*/ 0);
        s.f_off = true;
    }
}

static void scene_g_sysex8(m2device& midi, Showcase& s, uint32_t t) {
    if (s.g_done || t < kG_Ms) return;
    static const uint8_t payload[] = {
        0x7E, 0x7F, 0x06, 0x01,
        0xFF, 0xC3, 0xA1, 0xB2, 0xC4, 0xD5, 0xE6,
        0xF7, 0x00, 0x80, 0xAA, 0x55,
    };
    midi.sendSysEx8(/*group*/ 0, /*streamId*/ 1, payload, sizeof(payload));
    std::printf("[G] SysEx8 emitted (%zu raw 8-bit bytes)\r\n", sizeof(payload));
    s.g_done = true;
}

static void scene_h_dctpq(m2device& midi, Showcase& s, uint32_t t) {
    if (s.h_done || t < kH_Ms) return;
    midi.sendDctpq(/*tpq*/ 480);
    midi.sendDeltaClockstamp(/*ticks*/ 240);
    std::printf("[H] DCTPQ=480 + Delta Clockstamp=240 ticks\r\n");
    s.h_done = true;
}

static void scene_i_pe_notify(m2ci& ci, Showcase& s, uint32_t t) {
    if (s.i_done || t < kI_Ms) return;
    std::snprintf(g_overlay_rate, sizeof(g_overlay_rate),
                  "{\"rateHz\":%u}", (unsigned)(50 + s.cycle_count));
    ci.notifyPropertyChanged("OverlayRate");
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

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000LL);

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
    scene_g_sysex8(midi, s, t);
    scene_h_dctpq(midi, s, t);
    scene_i_pe_notify(ci, s, t);
    scene_j_clip_end(midi, s, t);
}

/*--------------------------------------------------------------------+
 * FreeRTOS entry point
 *--------------------------------------------------------------------*/
extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(200));

    std::printf("\r\n===========================================\r\n");
    std::printf("  esp32-s3-devkitc-usb-midi2-showcase  (VID:PID 0xCAFE:0x4090)\r\n");
    std::printf("===========================================\r\n");

    static m2device midi;
    static m2ci     ci(midi);

    esp32_s3_devkitc_midi2::init(midi, ci);
    midi.begin();
    midi.enableJRHeartbeat(500);
    ci.begin(kMfrId, kFamilyId, kModelId, kVersion);

    install_stream_responder(midi);
    install_ci_bootstrap(ci);

    midi.onNoteOn([](uint8_t ch, uint8_t note, uint16_t vel) {
        std::printf("[in] NoteOn ch=%u note=%u vel=%u\r\n",
                    (unsigned)ch, (unsigned)note, (unsigned)vel);
    });

    std::printf("[ready] entering main loop\r\n");

    static Showcase showcase{};
    bool prev_mounted = false;
    while (true) {
        esp32_s3_devkitc_midi2::task(midi);

        bool mounted = midi.isMounted();
        if (mounted != prev_mounted) {
            esp32_s3_devkitc_midi2::led_show_mounted(mounted);
            prev_mounted = mounted;
        }

        showcase_step(midi, ci, showcase);
        vTaskDelay(1);  // yield to FreeRTOS scheduler
    }
}

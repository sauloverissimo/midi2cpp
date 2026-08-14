/*
 * main.cpp: waveshare-rp2350-usb-a-bridge-midi2-showcase
 *
 * Transparent USB MIDI 2.0 bridge on the Waveshare RP2350-USB-A:
 *
 *   PC <- USB-C (rhport 0, native), Feather, USB-A (rhport 1, PIO-USB) -> upstream device
 *
 * All bridge behaviour lives in midi2::m2bridge (identity-bound slot,
 * group window, Stream Discovery, MIDI-CI faces, MIDI 1.0 uplift).
 * SSD1306 OLED shows live forwarded traffic with arrow markers:
 *   '>' upstream USB-A -> PC
 *   '<' PC             -> upstream USB-A
 */
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "bsp/board_api.h"

#include "feather_bridge.h"
#include "midi2_rx_ring.h"
#include "display.h"

namespace {

// ----------------------------------------------------------------------------
// Note name helper
// ----------------------------------------------------------------------------
const char* kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
void note_name(uint8_t pitch, char* buf, size_t len) {
    int octave = (pitch / 12) - 1;
    std::snprintf(buf, len, "%s%d", kNoteNames[pitch % 12], octave);
}

// ----------------------------------------------------------------------------
// Color hints (SSD1306 is mono, value preserved for any future colour port)
// ----------------------------------------------------------------------------
constexpr uint16_t COLOR_INFO       = 0x07E0;
constexpr uint16_t COLOR_WARN       = 0xF800;
constexpr uint16_t COLOR_UPSTREAM   = 0x07E0;
constexpr uint16_t COLOR_DOWNSTREAM = 0x07FF;
constexpr uint16_t COLOR_SYS        = 0xFFE0;

// ----------------------------------------------------------------------------
// Forwarded message stats, surfaced in the status bar
// ----------------------------------------------------------------------------
uint32_t g_count_upstream   = 0;
uint32_t g_count_downstream = 0;

// ----------------------------------------------------------------------------
// UMP -> short human-readable line. The bridge sees raw words, so we
// decode just enough to label what passed by. Group + status nibble +
// 1-2 payload bytes is plenty for monitoring.
// ----------------------------------------------------------------------------
void format_ump(const uint32_t* w, uint8_t count, char arrow,
                uint8_t group_arg, char* out, size_t cap) {
    uint8_t mt = (uint8_t)((w[0] >> 28) & 0x0F);
    uint8_t group = (mt >= 0x2 && mt <= 0x5)
                        ? (uint8_t)((w[0] >> 24) & 0x0F)
                        : group_arg;

    char nn[8] = {0};

    switch (mt) {
        case 0x0:  // Utility
            std::snprintf(out, cap, "%c U %08lX", arrow, (unsigned long)w[0]);
            return;
        case 0x1:  // System Real Time / Common
            std::snprintf(out, cap, "%c S g%u %08lX", arrow,
                          (unsigned)group, (unsigned long)w[0]);
            return;
        case 0x2: {  // MIDI 1.0 Channel Voice
            uint8_t status = (uint8_t)((w[0] >> 16) & 0xFF);
            uint8_t hi     = (status >> 4) & 0x0F;
            uint8_t ch     = status & 0x0F;
            uint8_t d1     = (uint8_t)((w[0] >> 8) & 0xFF);
            uint8_t d2     = (uint8_t)(w[0] & 0xFF);
            switch (hi) {
                case 0x9:  // NoteOn (or NoteOff if vel==0)
                    note_name(d1, nn, sizeof(nn));
                    if (d2 == 0) {
                        std::snprintf(out, cap, "%c g%u Off %s ch%u",
                                       arrow, (unsigned)group, nn, (unsigned)ch);
                    } else {
                        std::snprintf(out, cap, "%c g%u On %s ch%u v%u",
                                       arrow, (unsigned)group, nn,
                                       (unsigned)ch, (unsigned)d2);
                    }
                    break;
                case 0x8:
                    note_name(d1, nn, sizeof(nn));
                    std::snprintf(out, cap, "%c g%u Off %s ch%u",
                                   arrow, (unsigned)group, nn, (unsigned)ch);
                    break;
                case 0xB:
                    std::snprintf(out, cap, "%c g%u CC%u ch%u %u",
                                   arrow, (unsigned)group, (unsigned)d1,
                                   (unsigned)ch, (unsigned)d2);
                    break;
                case 0xE:
                    std::snprintf(out, cap, "%c g%u PB ch%u %u",
                                   arrow, (unsigned)group, (unsigned)ch,
                                   (unsigned)((d2 << 7) | d1));
                    break;
                default:
                    std::snprintf(out, cap, "%c g%u %02X %02X %02X",
                                   arrow, (unsigned)group,
                                   (unsigned)status, (unsigned)d1, (unsigned)d2);
                    break;
            }
            return;
        }
        case 0x3:  // SysEx7 (2 words)
            std::snprintf(out, cap, "%c g%u Sx7 %08lX",
                           arrow, (unsigned)group, (unsigned long)w[0]);
            return;
        case 0x4: {  // MIDI 2.0 Channel Voice (2 words)
            if (count < 2) {
                std::snprintf(out, cap, "%c g%u CV2?", arrow, (unsigned)group);
                return;
            }
            uint8_t status = (uint8_t)((w[0] >> 16) & 0xFF);
            uint8_t hi     = (status >> 4) & 0x0F;
            uint8_t ch     = status & 0x0F;
            uint8_t d1     = (uint8_t)((w[0] >> 8) & 0xFF);
            switch (hi) {
                case 0x9:
                    note_name(d1, nn, sizeof(nn));
                    std::snprintf(out, cap, "%c g%u On %s ch%u v%04X",
                                   arrow, (unsigned)group, nn, (unsigned)ch,
                                   (unsigned)((w[1] >> 16) & 0xFFFF));
                    break;
                case 0x8:
                    note_name(d1, nn, sizeof(nn));
                    std::snprintf(out, cap, "%c g%u Off %s ch%u",
                                   arrow, (unsigned)group, nn, (unsigned)ch);
                    break;
                case 0xB:
                    std::snprintf(out, cap, "%c g%u CC%u ch%u %08lX",
                                   arrow, (unsigned)group, (unsigned)d1,
                                   (unsigned)ch, (unsigned long)w[1]);
                    break;
                case 0xE:
                    std::snprintf(out, cap, "%c g%u PB ch%u %08lX",
                                   arrow, (unsigned)group, (unsigned)ch,
                                   (unsigned long)w[1]);
                    break;
                default:
                    std::snprintf(out, cap, "%c g%u CV2 %02X",
                                   arrow, (unsigned)group, (unsigned)status);
                    break;
            }
            return;
        }
        case 0x5:  // SysEx8 / Mixed Data Set (multi-word)
            std::snprintf(out, cap, "%c g%u Sx8 %uw",
                           arrow, (unsigned)group, (unsigned)count);
            return;
        case 0xD:  // Flex Data
            std::snprintf(out, cap, "%c g%u Flex %uw",
                           arrow, (unsigned)group, (unsigned)count);
            return;
        case 0xF:  // UMP Stream
            std::snprintf(out, cap, "%c Stream %02X",
                           arrow,
                           (unsigned)((w[0] >> 16) & 0x3FF));
            return;
        default:
            std::snprintf(out, cap, "%c MT%X %uw",
                           arrow, (unsigned)mt, (unsigned)count);
            return;
    }
}

midi2::m2bridge g_bridge;

// MIDI-CI identity + category backing for the bridge's own face (its
// Function Block on groups 5-16). Model id is fleet-unique.
constexpr uint8_t  kManufacturerId[3] = {0x7D, 0x00, 0x00};
constexpr uint16_t kFamily            = 0x0001;
constexpr uint16_t kModel             = 0x0016;   // fleet-unique (devices 0x0001.., bridges 0x0014..)
constexpr uint32_t kVersion           = 0x00000400;
constexpr const char* kEndpointName   = "RP2350 USB-A Bridge MIDI 2.0";
constexpr const char* kProductInstance = "rp2350-usb-a-bridge-0001";
const uint8_t kProfileId[5] = {0x7E, 0x00, 0x00, 0x01, 0x00};   // GM 1
const char kDeviceInfo[] =
    "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[22,0],"
     "\"versionId\":[0,0,4,0],\"manufacturer\":\"midi2.diy\","
     "\"family\":\"Bridge\",\"model\":\"RP2350 USB-A Bridge MIDI 2.0\","
     "\"version\":\"0.0.1\"}";
const char kChannelList[] = "[{\"title\":\"Bridge\",\"channel\":1}]";
const char kProgramList[] = "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]";

// The traffic tap runs inside the USB RX path: it must only enqueue.
// Rendering (blocking I2C flush, ~20 ms a frame) happens in the main
// loop, at most one message per tick.
static midi2::RxRing<32> g_tap_ring;

void install_callbacks() {
    g_bridge.onTraffic([](bool to_pc, const uint32_t* words, uint8_t count) {
        g_tap_ring.push(to_pc ? 1 : 0, words, count);
        if (to_pc) ++g_count_upstream; else ++g_count_downstream;
    });
}

void render_tap_tick() {
    midi2::RxRing<32>::Slot rec;
    if (!g_tap_ring.pop(rec)) return;
    char line[32];
    format_ump(rec.ump, rec.words, rec.idx ? '>' : '<', 0, line, sizeof(line));
    display_log(line, rec.idx ? COLOR_UPSTREAM : COLOR_DOWNSTREAM);
}

// ----------------------------------------------------------------------------
// Standalone showcase: when the PC is mounted but no upstream device is
// plugged into the USB-A port, the bridge emits its own MIDI 2.0 traffic
// so a connected DAW can validate the link without a hardware source.
//
// 6 s cycle (mirrors the spirit of rp2040-midi2-showcase, condensed):
//   - Chromatic C4->B4 walk: NoteOn/Off every 250 ms (24 steps total)
//   - Every 6 s: a short CC#74 32-bit sweep on ch0
//
// All UMPs are MIDI 2.0 Channel Voice (MT 0x4) on the bridge's own
// Function Block (group index 4; slot 0 owns groups 0-3), ch 0.
// Emission stops automatically when an upstream device mounts; the
// forward path then takes over.
// ----------------------------------------------------------------------------
void emit_note_step(uint32_t step) {
    bool note_on  = (step % 2) == 0;
    uint8_t note  = (uint8_t)(60 + ((step / 2) % 12));  // C4..B4
    uint8_t status = note_on ? 0x90 : 0x80;
    uint32_t w[2];
    w[0] = ((uint32_t)0x4 << 28)
         | ((uint32_t)0x4 << 24)      // bridge FB first group
         | ((uint32_t)status << 16)
         | ((uint32_t)note << 8);
    w[1] = note_on ? ((uint32_t)0xC000u << 16) : 0;
    if (feather_bridge::send_to_pc(w, 2)) {
        char nn[8]; note_name(note, nn, sizeof(nn));
        char line[24];
        std::snprintf(line, sizeof(line), "show %s %s",
                       note_on ? "On" : "Off", nn);
        display_log(line, note_on ? COLOR_INFO : COLOR_DOWNSTREAM);
    }
}

void emit_cc_sweep() {
    // 5 points across the 32-bit range, ch 0, CC #74 (Brightness).
    static const uint32_t kValues[5] = {
        0x10000000u, 0x40000000u, 0x80000000u, 0xC0000000u, 0xFFFFFFFFu
    };
    for (uint8_t i = 0; i < 5; ++i) {
        uint32_t w[2];
        w[0] = ((uint32_t)0x4 << 28)
             | ((uint32_t)0x4 << 24)      // bridge FB first group
             | ((uint32_t)0xB0 << 16)   // CC ch 0
             | ((uint32_t)74 << 8);     // CC#74
        w[1] = kValues[i];
        if (!feather_bridge::send_to_pc(w, 2)) return;
    }
    display_log("show CC74 sweep", COLOR_INFO);
}

void showcase_tick(uint32_t now_ms) {
    static uint32_t last_note_ms = 0;
    static uint32_t step         = 0;
    static uint32_t last_cc_ms   = 0;

    if (now_ms - last_note_ms >= 250) {
        last_note_ms = now_ms;
        emit_note_step(step);
        step++;
    }
    if (now_ms - last_cc_ms >= 6000) {
        last_cc_ms = now_ms;
        emit_cc_sweep();
    }
}

}  // namespace

int main() {
    stdio_init_all();
    sleep_ms(200);

    display_init();
    sleep_ms(1500);

    // Single upstream port: slot 0 = groups 1-4, bridge FB = groups 5-16.
    g_bridge.setNumSlots(1);
    g_bridge.setManufacturerId(kManufacturerId);
    g_bridge.setFamily(kFamily);
    g_bridge.setModel(kModel);
    g_bridge.setVersion(kVersion);
    g_bridge.setEndpointName(kEndpointName);
    g_bridge.setProductInstanceId(kProductInstance);

    install_callbacks();
    feather_bridge::init(g_bridge);

    g_bridge.ci().addProfile(kProfileId, /*alwaysOn*/ false);
    g_bridge.ci().addPropertyStatic("DeviceInfo",  kDeviceInfo);
    g_bridge.ci().addPropertyStatic("ChannelList", kChannelList);
    g_bridge.ci().addPropertyStatic("ProgramList", kProgramList);
    g_bridge.ci().setMidiReport(/*msg_data_control*/ 0x01,
                                /*system bitmap*/    0x00000000FFFFFFFFull,
                                /*channel bitmap*/   0xFFFFFFFFFFFFFFFFull,
                                /*note bitmap*/      0xFFFFFFFFFFFFFFFFull);

    display_live_begin();
    display_status("Waiting...");

    uint32_t last_status_ms = 0;
    enum class Mode { Waiting, Showcase, Bridging };
    Mode mode = Mode::Waiting;

    while (true) {
        feather_bridge::task(g_bridge);
        render_tap_tick();

        uint32_t now = (uint32_t)(time_us_64() / 1000ULL);
        bool pc_present       = feather_bridge::downstream_present();
        bool upstream_present = feather_bridge::upstream_present();

        Mode new_mode = !pc_present                  ? Mode::Waiting
                       : !upstream_present           ? Mode::Showcase
                                                     : Mode::Bridging;

        if (new_mode != mode) {
            mode = new_mode;
            display_live_begin();
            switch (mode) {
                case Mode::Waiting:
                    display_log("Waiting for PC", COLOR_WARN);
                    break;
                case Mode::Showcase:
                    display_log("Standalone showcase", COLOR_SYS);
                    display_status("Showcase");
                    break;
                case Mode::Bridging:
                    display_log("Bridging UMP", COLOR_INFO);
                    display_status("Bridging");
                    break;
            }
            last_status_ms = 0;
        }

        switch (mode) {
            case Mode::Waiting:
                if (now - last_status_ms > 200) {
                    last_status_ms = now;
                    display_connecting(now);
                }
                break;

            case Mode::Showcase:
                showcase_tick(now);
                if (now - last_status_ms > 2000) {
                    last_status_ms = now;
                    display_status("Showcase");
                }
                break;

            case Mode::Bridging:
                if (now - last_status_ms > 2000) {
                    last_status_ms = now;
                    char line[24];
                    std::snprintf(line, sizeof(line), ">%lu <%lu",
                                   (unsigned long)g_count_upstream,
                                   (unsigned long)g_count_downstream);
                    display_status(line);
                }
                break;
        }
    }
}

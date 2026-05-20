/*
 * main.cpp: waveshare-rp2350-usb-a-bridge-midi2-showcase
 *
 * Transparent USB MIDI 2.0 bridge on the Waveshare RP2350-USB-A:
 *
 *   PC <- USB-C (rhport 0, native), Feather, USB-A (rhport 1, PIO-USB) -> upstream device
 *
 * UMP flows raw between the two stacks via ump_router. SSD1306 OLED
 * shows live forwarded traffic with arrow markers:
 *   '>' upstream USB-A -> PC
 *   '<' PC             -> upstream USB-A
 *
 * Upstream MIDI 1.0 (alt=0) devices are uplifted to UMP MT 0x2 so the
 * PC always sees clean MIDI 2.0. PC->upstream MIDI 1.0 conversion is
 * v0.2 work; the bridge logs a warning and drops those messages.
 */
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "bsp/board_api.h"

#include "feather_bridge.h"
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

void install_callbacks() {
    feather_bridge::onHostMount([](uint8_t idx, uint8_t protocol_version) {
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] %s mounted",
                       (unsigned)idx,
                       protocol_version >= 1 ? "MIDI 2.0" : "MIDI 1.0");
        display_log(line, COLOR_INFO);
        display_status("Bridging");
    });

    feather_bridge::onHostUnmount([](uint8_t idx) {
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] unmounted", (unsigned)idx);
        display_log(line, COLOR_WARN);
        display_status("Waiting...");
    });

    feather_bridge::onDeviceMount([] {
        display_log("PC mounted", COLOR_SYS);
    });

    feather_bridge::onDeviceUnmount([] {
        display_log("PC unmounted", COLOR_WARN);
    });

    feather_bridge::onForwardUpstream(
        [](const uint32_t* words, uint8_t count) {
            char line[32];
            format_ump(words, count, '>', 0, line, sizeof(line));
            display_log(line, COLOR_UPSTREAM);
            ++g_count_upstream;
        });

    feather_bridge::onForwardDownstream(
        [](const uint32_t* words, uint8_t count) {
            char line[32];
            format_ump(words, count, '<', 0, line, sizeof(line));
            display_log(line, COLOR_DOWNSTREAM);
            ++g_count_downstream;
        });

    feather_bridge::onDrop([](ump_source_t src, uint32_t total) {
        char line[32];
        std::snprintf(line, sizeof(line), "drop %s n=%lu",
                       src == UMP_SOURCE_HOST ? "host" : "dev",
                       (unsigned long)total);
        display_log(line, COLOR_WARN);
    });
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
// All UMPs are MIDI 2.0 Channel Voice (MT 0x4) on group 0, ch 0.
// Emission stops automatically when an upstream device mounts; the
// forward path then takes over.
// ----------------------------------------------------------------------------
void emit_note_step(uint32_t step) {
    bool note_on  = (step % 2) == 0;
    uint8_t note  = (uint8_t)(60 + ((step / 2) % 12));  // C4..B4
    uint8_t status = note_on ? 0x90 : 0x80;
    uint32_t w[2];
    w[0] = ((uint32_t)0x4 << 28)
         | ((uint32_t)0x0 << 24)
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
             | ((uint32_t)0x0 << 24)
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

    install_callbacks();
    feather_bridge::init();

    display_live_begin();
    display_status("Waiting...");

    uint32_t last_status_ms = 0;
    enum class Mode { Waiting, Showcase, Bridging };
    Mode mode = Mode::Waiting;

    while (true) {
        feather_bridge::task();

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

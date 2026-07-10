/*
 * scene_display.cpp, Scene Cinema visualiser implementation. See
 * scene_display.h for the public contract.
 *
 * Render loop runs on RP2040 core1 at ~30 fps and is decoupled from
 * the showcase cycle on core0. Shared state is plain scalars whose
 * writes are atomic on the RP2040 (32-bit aligned, single store per
 * field); core1 samples a snapshot at the top of every frame, so a
 * torn read across two fields yields a one-frame visual glitch at
 * worst: never invalid state.
 */
#include "scene_display.h"
#include "lgfx_user.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace scene_display {
namespace {

// --------------------------------------------------------------------------
// Geometry
// --------------------------------------------------------------------------
constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kHeaderH = 24;
constexpr int kFooterH = 22;
constexpr int kMainY   = kHeaderH;
constexpr int kMainH   = kScreenH - kHeaderH - kFooterH;   // 89 px

// --------------------------------------------------------------------------
// Palette (RGB565)
// --------------------------------------------------------------------------
constexpr uint16_t kBg         = 0x0000;  // black
constexpr uint16_t kPanel      = 0x1082;  // very dark grey
constexpr uint16_t kHeaderBgL  = 0xE2A4;  // LilyGO red
constexpr uint16_t kHeaderBgR  = 0xFB46;  // amber
constexpr uint16_t kFooterBg   = 0x18C3;  // dark slate
constexpr uint16_t kInk        = 0xFFFF;  // white
constexpr uint16_t kInkDim     = 0xC618;  // light grey
constexpr uint16_t kAccentA    = 0x07FF;  // cyan
constexpr uint16_t kAccentB    = 0xF81F;  // magenta
constexpr uint16_t kAccentC    = 0xFFE0;  // yellow
constexpr uint16_t kAccentD    = 0x07E0;  // green
constexpr uint16_t kAccentE    = 0xFD60;  // orange
constexpr uint16_t kAccentF    = 0xFC18;  // pink

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// --------------------------------------------------------------------------
// Display + framebuffer sprite
// --------------------------------------------------------------------------
LGFX_TPicoC3 g_lcd;
LGFX_Sprite  g_canvas(&g_lcd);

// --------------------------------------------------------------------------
// Shared state (writers on core0, single reader on core1)
// --------------------------------------------------------------------------
std::atomic<uint8_t>  g_scene{(uint8_t)Scene::None};
std::atomic<uint32_t> g_cycle{0};
std::atomic<uint32_t> g_progress_q16{0};   // fixed-point 0..65535
std::atomic<bool>     g_mounted{false};

std::atomic<uint32_t> g_cnt_note_on{0};
std::atomic<uint32_t> g_cnt_note_off{0};
std::atomic<uint32_t> g_cnt_cc{0};
std::atomic<uint32_t> g_cnt_pb{0};
std::atomic<uint32_t> g_cnt_other{0};

// Scene A
std::atomic<uint16_t> g_a_bpm_x100{12000};
std::atomic<uint8_t>  g_a_time_num{4};
std::atomic<uint8_t>  g_a_time_den{4};
char                  g_a_chord[16] = "Cmaj7";

// Scene B
std::atomic<uint8_t>  g_b_note{60};
std::atomic<int16_t>  g_b_pb_phase_q15{0};      // -32767..+32767

// Scene C
std::atomic<uint8_t>  g_c_note{72};
std::atomic<uint16_t> g_c_vel16{0};
std::atomic<uint32_t> g_c_cc32{0};
std::atomic<uint32_t> g_c_pb32{0x80000000u};
std::atomic<uint8_t>  g_c_step{0};

// Scene D
std::atomic<uint8_t>  g_d_program{0};
std::atomic<uint8_t>  g_d_bank_msb{0};
std::atomic<uint8_t>  g_d_bank_lsb{0};

// Scene E
std::atomic<uint8_t>  g_e_flags{0};  // bit 0 RPN, 1 NRPN, 2 RelRPN, 3 RelNRPN
char                  g_e_last_label[24] = "";

// Scene F
std::atomic<uint8_t>  g_f_note{64};
std::atomic<int16_t>  g_f_cents{0};

// Scene G
uint8_t               g_g_bytes[16] = {0};
std::atomic<uint8_t>  g_g_count{0};

// Scene H
std::atomic<uint16_t> g_h_tpq{0};
std::atomic<uint32_t> g_h_delta{0};

// Scene I
char                  g_i_property[24] = "";
std::atomic<uint8_t>  g_i_subscribers{0};

// Scene J: fade phase starts at 255, fades to 0 over ~1.5 s.
std::atomic<int16_t>  g_j_fade{0};
std::atomic<uint32_t> g_j_started_ms{0};

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
const char* note_name(uint8_t n, char buf[6]) {
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int octave = (int)(n / 12) - 1;
    std::snprintf(buf, 6, "%s%d", names[n % 12], octave);
    return buf;
}

const char* scene_label(Scene s) {
    switch (s) {
        case Scene::A_FlexData:         return "A | Flex Data";
        case Scene::B_PerNote:          return "B | Per-Note";
        case Scene::C_Resolution:       return "C | 32-bit Resolution";
        case Scene::D_ProgramBank:      return "D | Program + Bank";
        case Scene::E_RpnNrpn:          return "E | RPN / NRPN";
        case Scene::F_NoteAttribute:    return "F | Note Attribute";
        case Scene::G_SysEx7:           return "G | SysEx7";
        case Scene::H_DeltaClockstamp:  return "H | Delta Clockstamp";
        case Scene::I_PENotify:         return "I | PE Notify";
        case Scene::J_EndOfClip:        return "J | End of Clip";
        default:                        return "Booting";
    }
}

uint16_t scene_accent(Scene s) {
    switch (s) {
        case Scene::A_FlexData:         return kAccentA;
        case Scene::B_PerNote:          return kAccentB;
        case Scene::C_Resolution:       return kAccentC;
        case Scene::D_ProgramBank:      return kAccentD;
        case Scene::E_RpnNrpn:          return kAccentE;
        case Scene::F_NoteAttribute:    return kAccentF;
        case Scene::G_SysEx7:           return kAccentA;
        case Scene::H_DeltaClockstamp:  return kAccentC;
        case Scene::I_PENotify:         return kAccentB;
        case Scene::J_EndOfClip:        return kInkDim;
        default:                        return kInk;
    }
}

// Horizontal gradient fill: x..x+w, two endpoint colours.
void fill_gradient(int x, int y, int w, int h, uint16_t cL, uint16_t cR) {
    int rL = (cL >> 11) & 0x1F, gL = (cL >> 5) & 0x3F, bL = cL & 0x1F;
    int rR = (cR >> 11) & 0x1F, gR = (cR >> 5) & 0x3F, bR = cR & 0x1F;
    for (int i = 0; i < w; ++i) {
        int r = rL + (rR - rL) * i / (w - 1);
        int g = gL + (gR - gL) * i / (w - 1);
        int b = bL + (bR - bL) * i / (w - 1);
        uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
        g_canvas.drawFastVLine(x + i, y, h, c);
    }
}

// --------------------------------------------------------------------------
// Header + Footer
// --------------------------------------------------------------------------
void draw_header(Scene s, uint32_t cycle, bool mounted) {
    fill_gradient(0, 0, kScreenW, kHeaderH, kHeaderBgL, kHeaderBgR);

    // Mount indicator: small filled circle on the right.
    uint16_t led = mounted ? kAccentD : 0x4208;
    g_canvas.fillCircle(kScreenW - 12, kHeaderH / 2, 4, led);

    // "MIDI 2.0" badge on the left.
    g_canvas.setTextColor(kInk, 0);
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    g_canvas.drawString("MIDI 2.0", 6, kHeaderH / 2);

    // Cycle counter mid-left.
    g_canvas.setFont(&fonts::Font0);
    char cb[16];
    std::snprintf(cb, sizeof(cb), "#%lu", (unsigned long)cycle);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_right);
    g_canvas.drawString(cb, kScreenW - 24, kHeaderH / 2);

    // Scene label centered.
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    g_canvas.drawString(scene_label(s), kScreenW / 2, kHeaderH / 2);
}

void draw_footer(Scene s, float progress) {
    g_canvas.fillRect(0, kScreenH - kFooterH, kScreenW, kFooterH, kFooterBg);

    // Progress bar fills the very last 4 px.
    int pw = (int)(progress * kScreenW);
    if (pw < 0) pw = 0;
    if (pw > kScreenW) pw = kScreenW;
    g_canvas.fillRect(0, kScreenH - 4, pw, 4, scene_accent(s));
    g_canvas.fillRect(pw, kScreenH - 4, kScreenW - pw, 4, kPanel);

    // Counters: NoteOn / CC / PB (most informative trio).
    char buf[40];
    std::snprintf(buf, sizeof(buf), "On %lu  CC %lu  PB %lu",
                  (unsigned long)g_cnt_note_on.load(std::memory_order_relaxed),
                  (unsigned long)g_cnt_cc.load(std::memory_order_relaxed),
                  (unsigned long)g_cnt_pb.load(std::memory_order_relaxed));
    g_canvas.setTextColor(kInkDim, 0);
    g_canvas.setFont(&fonts::Font0);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    g_canvas.drawString(buf, 6, kScreenH - kFooterH + (kFooterH - 4) / 2);

    // MT badge on the right.
    static const char* mt_badge[] = {
        "",              // None
        "MT 0xD/0xF",    // A
        "MT 0x4",        // B
        "MT 0x4",        // C
        "MT 0x4",        // D
        "MT 0x4",        // E
        "MT 0x4 attr",   // F
        "MT 0x5",        // G
        "MT 0x0",        // H
        "MIDI-CI PE",    // I
        "MT 0xF status", // J
    };
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_right);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.drawString(mt_badge[(uint8_t)s], kScreenW - 6,
                        kScreenH - kFooterH + (kFooterH - 4) / 2);
}

// --------------------------------------------------------------------------
// Scene renderers
// --------------------------------------------------------------------------

// A: Flex Data: tempo + time sig + chord + animated metronome.
void render_a(float local_phase) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    uint16_t bpm = g_a_bpm_x100.load() / 100;
    char buf[16];

    // BPM big
    std::snprintf(buf, sizeof(buf), "%u", bpm);
    g_canvas.setFont(&fonts::Font7);
    g_canvas.setTextColor(kAccentA, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    g_canvas.drawString(buf, 12, kMainY + 28);
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kInkDim, 0);
    g_canvas.drawString("BPM", 12, kMainY + 58);

    // Time sig
    std::snprintf(buf, sizeof(buf), "%u/%u",
                  g_a_time_num.load(), g_a_time_den.load());
    g_canvas.setFont(&fonts::Font4);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    g_canvas.drawString(buf, kScreenW - 60, kMainY + 26);

    // Chord
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kAccentE, 0);
    g_canvas.drawString(g_a_chord, kScreenW - 60, kMainY + 58);

    // Metronome bar pulsing at 2 Hz
    float pulse = 0.5f + 0.5f * sinf(local_phase * 2.0f * 3.14159265f * 2.0f);
    int bar_w = (int)(pulse * 200);
    g_canvas.fillRect((kScreenW - bar_w) / 2, kMainY + kMainH - 8, bar_w, 4, kAccentA);
}

// B: Per-Note PB: piano key + animated sine vibrato.
void render_b(float local_phase) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    uint8_t note = g_b_note.load();
    char nb[6];
    note_name(note, nb);

    // Piano: 13 keys (C..C), highlight active note.
    int key_w = 13;
    int piano_x = 14;
    int piano_y = kMainY + 6;
    int piano_h = 36;

    static const bool black[12] = {0,1,0,1,0,0,1,0,1,0,1,0};
    int white_i = 0;
    for (int n = 0; n < 12; ++n) {
        if (!black[n]) {
            int x = piano_x + white_i * key_w;
            bool active = (note % 12) == n;
            g_canvas.fillRect(x, piano_y, key_w - 1, piano_h,
                              active ? kAccentB : kInk);
            g_canvas.drawRect(x, piano_y, key_w - 1, piano_h, kBg);
            ++white_i;
        }
    }
    // Black keys overlaid
    white_i = 0;
    for (int n = 0; n < 12; ++n) {
        if (!black[n]) { ++white_i; continue; }
        // Black sits between white_i-1 and white_i.
        int x = piano_x + white_i * key_w - (key_w / 2) - 1;
        bool active = (note % 12) == n;
        g_canvas.fillRect(x, piano_y, key_w - 2, piano_h * 6 / 10,
                          active ? kAccentB : kBg);
    }

    // Note label
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kAccentB, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    g_canvas.drawString(nb, piano_x + 13 * key_w + 6, piano_y + piano_h / 2);

    // Vibrato sine wave: full width, amplitude scaled by current PB phase.
    int phase_q15 = g_b_pb_phase_q15.load();
    float amp = (float)phase_q15 / 32767.0f;   // -1..+1
    if (amp < 0) amp = -amp;
    int wave_y = kMainY + kMainH - 18;
    int wave_h = 16;
    int prev_y = wave_y;
    for (int x = 0; x < kScreenW; ++x) {
        float v = sinf((float)x / kScreenW * 4.0f * 3.14159265f
                       + local_phase * 10.0f);
        int y = wave_y + (int)(v * amp * (wave_h / 2));
        if (x > 0) g_canvas.drawLine(x - 1, prev_y, x, y, kAccentB);
        prev_y = y;
    }

    g_canvas.setTextColor(kInkDim, 0);
    g_canvas.setFont(&fonts::Font0);
    g_canvas.setTextDatum(lgfx::textdatum_t::bottom_left);
    g_canvas.drawString("Per-Note PB  5 Hz", 6, kMainY + kMainH - 2);
}

// C: Resolution: 4 vertical bars (CC32, PB32, Poly, ChP) + tiny 7-bit ref.
void render_c(float /*local_phase*/) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    auto bar_h_for = [](uint32_t v32) {
        return (int)((uint64_t)v32 * (kMainH - 24) / 0xFFFFFFFFu);
    };
    auto draw_bar = [&](int x, int w, int filled_h, uint16_t color,
                        const char* lbl) {
        int y0 = kMainY + 8;
        int h  = kMainH - 24;
        g_canvas.drawRect(x, y0, w, h, kInkDim);
        g_canvas.fillRect(x + 1, y0 + h - filled_h, w - 2, filled_h, color);
        g_canvas.setFont(&fonts::Font0);
        g_canvas.setTextColor(kInkDim, 0);
        g_canvas.setTextDatum(lgfx::textdatum_t::top_center);
        g_canvas.drawString(lbl, x + w / 2, y0 + h + 2);
    };

    uint32_t cc = g_c_cc32.load();
    uint32_t pb = g_c_pb32.load();
    uint16_t vel = g_c_vel16.load();

    int bw = 32;
    int gap = 16;
    int total = 4 * bw + 3 * gap;
    int x0 = (kScreenW - total) / 2;

    draw_bar(x0,                bw, bar_h_for(cc),               kAccentC, "CC");
    draw_bar(x0 + (bw + gap),   bw, bar_h_for(pb),               kAccentA, "PB");
    draw_bar(x0 + 2*(bw + gap), bw, bar_h_for((uint32_t)vel << 16), kAccentB, "Vel");
    draw_bar(x0 + 3*(bw + gap), bw, bar_h_for(cc / 2 + pb / 4),  kAccentE, "Pres");

    // Note + step marker
    char nb[6];
    note_name(g_c_note.load(), nb);
    char top[24];
    std::snprintf(top, sizeof(top), "%s   step %u/8", nb, g_c_step.load());
    g_canvas.setFont(&fonts::Font0);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::top_right);
    g_canvas.drawString(top, kScreenW - 6, kMainY + 2);
}

// D: Program + Bank: huge program number + bank below.
void render_d(float /*local_phase*/) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%u", g_d_program.load());
    g_canvas.setFont(&fonts::Font7);
    g_canvas.setTextColor(kAccentD, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    g_canvas.drawString(buf, kScreenW / 2, kMainY + 30);

    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kInkDim, 0);
    g_canvas.drawString("PROGRAM", kScreenW / 2, kMainY + 56);

    std::snprintf(buf, sizeof(buf), "Bank  MSB 0x%02X   LSB 0x%02X",
                  g_d_bank_msb.load(), g_d_bank_lsb.load());
    g_canvas.setFont(&fonts::Font0);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.drawString(buf, kScreenW / 2, kMainY + 76);
}

// E: RPN / NRPN: 4 quadrants, last triggered glows accent colour.
void render_e(float local_phase) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    uint8_t flags = g_e_flags.load();
    static const char* labels[4] = {"RPN", "NRPN", "Rel RPN", "Rel NRPN"};
    uint16_t fills[4] = {kAccentE, kAccentA, kAccentE, kAccentA};
    int qw = kScreenW / 2;
    int qh = kMainH / 2;
    float glow = 0.6f + 0.4f * sinf(local_phase * 6.28f);
    for (int i = 0; i < 4; ++i) {
        int x = (i & 1) ? qw : 0;
        int y = kMainY + ((i & 2) ? qh : 0);
        bool active = flags & (1u << i);
        uint16_t bg = kPanel;
        if (active) {
            int rr = (fills[i] >> 11) & 0x1F;
            int gg = (fills[i] >> 5) & 0x3F;
            int bb = fills[i] & 0x1F;
            bg = (uint16_t)((int)(rr * glow) << 11) | (uint16_t)((int)(gg * glow) << 5)
               | (uint16_t)(int)(bb * glow);
        }
        g_canvas.fillRect(x + 2, y + 2, qw - 4, qh - 4, bg);
        g_canvas.drawRect(x + 2, y + 2, qw - 4, qh - 4, kInkDim);

        g_canvas.setFont(&fonts::Font2);
        g_canvas.setTextColor(active ? kInk : kInkDim, 0);
        g_canvas.setTextDatum(lgfx::textdatum_t::middle_center);
        g_canvas.drawString(labels[i], x + qw / 2, y + qh / 2);
    }

    if (g_e_last_label[0]) {
        g_canvas.setFont(&fonts::Font0);
        g_canvas.setTextColor(kInk, 0);
        g_canvas.setTextDatum(lgfx::textdatum_t::bottom_center);
        g_canvas.drawString(g_e_last_label, kScreenW / 2, kMainY + kMainH - 2);
    }
}

// F: Note Attribute: piano key E4 highlighted + microtonal arrow + cents.
void render_f(float local_phase) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    uint8_t note = g_f_note.load();
    int16_t cents = g_f_cents.load();

    // Microtonal arrow pointing up if positive, down if negative.
    int cx = kScreenW / 2;
    int cy = kMainY + 30;
    bool up = cents >= 0;
    int dir = up ? -1 : +1;
    float wiggle = sinf(local_phase * 6.28f * 3.0f) * 2.0f;
    int ax = cx + (int)wiggle;
    int ay = cy + dir * 12;
    g_canvas.fillTriangle(ax - 8, cy, ax + 8, cy, ax, ay, kAccentF);
    g_canvas.drawLine(ax, cy, ax, cy + dir * 24, kAccentF);

    // Cents big
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%+d", (int)cents);
    g_canvas.setFont(&fonts::Font4);
    g_canvas.setTextColor(kAccentF, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::top_left);
    g_canvas.drawString(buf, cx + 18, kMainY + 18);
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kInkDim, 0);
    g_canvas.drawString("cents", cx + 18, kMainY + 42);

    // Note name
    char nb[6];
    note_name(note, nb);
    g_canvas.setFont(&fonts::Font4);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::top_center);
    g_canvas.drawString(nb, cx - 60, kMainY + 18);

    // Attribute label
    g_canvas.setFont(&fonts::Font0);
    g_canvas.setTextColor(kInkDim, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::bottom_center);
    g_canvas.drawString("attribute_type = pitch_7_9 (0x03)",
                        kScreenW / 2, kMainY + kMainH - 2);
}

// G: SysEx7: scrolling hex bytes.
void render_g(float local_phase) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    uint8_t n = g_g_count.load();
    if (n > 16) n = 16;

    int scroll = (int)(local_phase * 30.0f) % 16;

    g_canvas.setFont(&fonts::Font4);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    int x = 6 - scroll;
    int y = kMainY + 26;
    for (int i = 0; i < n && x < kScreenW; ++i) {
        char b[3];
        std::snprintf(b, sizeof(b), "%02X", g_g_bytes[i]);
        // Universal SysEx markers (0x7E/0x7F) highlighted.
        bool marker = g_g_bytes[i] == 0x7E || g_g_bytes[i] == 0x7F;
        g_canvas.setTextColor(marker ? kAccentA : kInkDim, 0);
        g_canvas.drawString(b, x, y);
        x += 30;
    }

    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::bottom_left);
    g_canvas.drawString("SysEx7 - Universal Identity Reply",
                        6, kMainY + kMainH - 6);
}

// H: Delta Clockstamp: timeline with tick marks + delta marker.
void render_h(float /*local_phase*/) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    uint16_t tpq = g_h_tpq.load();
    uint32_t delta = g_h_delta.load();
    int line_y = kMainY + kMainH / 2;
    g_canvas.drawFastHLine(12, line_y, kScreenW - 24, kInkDim);

    // Tick marks every quarter (4 quarters drawn)
    for (int q = 0; q <= 4; ++q) {
        int x = 12 + q * (kScreenW - 24) / 4;
        g_canvas.drawFastVLine(x, line_y - 6, 12, kInk);
    }

    // Delta marker: position relative to tpq
    if (tpq > 0) {
        float frac = (float)delta / (float)tpq;
        int x = 12 + (int)(frac * (kScreenW - 24) / 4);
        g_canvas.fillTriangle(x - 5, line_y - 14, x + 5, line_y - 14, x, line_y - 4, kAccentC);
    }

    char buf[24];
    std::snprintf(buf, sizeof(buf), "TPQ %u   delta %lu",
                  tpq, (unsigned long)delta);
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::bottom_center);
    g_canvas.drawString(buf, kScreenW / 2, kMainY + kMainH - 4);
}

// I: PE Notify: radiating wave + property + subscribers.
void render_i(float local_phase) {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);

    int cx = 40;
    int cy = kMainY + kMainH / 2;
    for (int r = 0; r < 4; ++r) {
        float t = local_phase * 2.0f + (float)r * 0.25f;
        int radius = 6 + (int)((sinf(t * 6.28f) * 0.5f + 0.5f) * 24);
        g_canvas.drawCircle(cx, cy, radius, kAccentB);
    }
    g_canvas.fillCircle(cx, cy, 4, kAccentB);

    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    g_canvas.drawString("PE Notify", 90, kMainY + 22);

    g_canvas.setFont(&fonts::Font4);
    g_canvas.setTextColor(kAccentB, 0);
    g_canvas.drawString(g_i_property, 90, kMainY + 48);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u subs", g_i_subscribers.load());
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kInkDim, 0);
    g_canvas.drawString(buf, 90, kMainY + 74);
}

// J: End of Clip: fade-to-black + centered label.
void render_j(float /*local_phase*/) {
    int16_t fade = g_j_fade.load();
    if (fade < 0) fade = 0;
    if (fade > 255) fade = 255;
    uint8_t alpha = (uint8_t)((255 - fade) * 0.3f);
    // Translate fade to a dim grey overlay on the existing image.
    uint16_t bg_dim = rgb565(alpha, alpha, alpha);
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, bg_dim);

    g_canvas.setFont(&fonts::Font4);
    g_canvas.setTextColor(kInk, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    g_canvas.drawString("End of Clip", kScreenW / 2, kMainY + kMainH / 2);
}

void render_none() {
    g_canvas.fillRect(0, kMainY, kScreenW, kMainH, kBg);
    g_canvas.setFont(&fonts::Font2);
    g_canvas.setTextColor(kInkDim, 0);
    g_canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    g_canvas.drawString("waiting for USB host", kScreenW / 2, kMainY + kMainH / 2);
}

// --------------------------------------------------------------------------
// Render loop (core1)
// --------------------------------------------------------------------------
void core1_render() {
    g_canvas.setColorDepth(16);
    g_canvas.createSprite(kScreenW, kScreenH);

    uint32_t prev = (uint32_t)(time_us_64() / 1000ULL);
    float    local_phase = 0.0f;

    while (true) {
        uint32_t now = (uint32_t)(time_us_64() / 1000ULL);
        float dt = (now - prev) / 1000.0f;
        prev = now;
        local_phase += dt;
        if (local_phase > 1000.0f) local_phase -= 1000.0f;

        Scene s = (Scene)g_scene.load();
        uint32_t cycle = g_cycle.load();
        float progress = (float)g_progress_q16.load() / 65535.0f;
        bool mounted = g_mounted.load();

        g_canvas.fillScreen(kBg);
        draw_header(s, cycle, mounted);

        switch (s) {
            case Scene::A_FlexData:        render_a(local_phase); break;
            case Scene::B_PerNote:         render_b(local_phase); break;
            case Scene::C_Resolution:      render_c(local_phase); break;
            case Scene::D_ProgramBank:     render_d(local_phase); break;
            case Scene::E_RpnNrpn:         render_e(local_phase); break;
            case Scene::F_NoteAttribute:   render_f(local_phase); break;
            case Scene::G_SysEx7:          render_g(local_phase); break;
            case Scene::H_DeltaClockstamp: render_h(local_phase); break;
            case Scene::I_PENotify:        render_i(local_phase); break;
            case Scene::J_EndOfClip:       render_j(local_phase); break;
            default:                       render_none();         break;
        }

        draw_footer(s, progress);
        g_canvas.pushSprite(0, 0);

        // Cap at ~30 fps.
        sleep_ms(33);
    }
}

}  // namespace

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------
void init() {
    g_lcd.init();
    g_lcd.setRotation(1);     // landscape, USB connector on the left
    g_lcd.setBrightness(180);
    g_lcd.fillScreen(kBg);

    multicore_launch_core1(core1_render);
}

void set_scene(Scene s, uint32_t cycle_count) {
    g_scene.store((uint8_t)s);
    g_cycle.store(cycle_count);
    if (s == Scene::J_EndOfClip) {
        g_j_started_ms.store((uint32_t)(time_us_64() / 1000ULL));
        g_j_fade.store(255);
    }
}

void set_progress(float fraction) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    g_progress_q16.store((uint32_t)(fraction * 65535.0f));
}

void set_mounted(bool mounted) { g_mounted.store(mounted); }

void bump_counter(CounterKind k) {
    switch (k) {
        case CounterKind::NoteOn:    g_cnt_note_on.fetch_add(1); break;
        case CounterKind::NoteOff:   g_cnt_note_off.fetch_add(1); break;
        case CounterKind::CC:        g_cnt_cc.fetch_add(1); break;
        case CounterKind::PitchBend: g_cnt_pb.fetch_add(1); break;
        case CounterKind::Other:     g_cnt_other.fetch_add(1); break;
    }
}

void notify_flex(uint16_t bpm_x100, uint8_t num, uint8_t den, const char* chord) {
    g_a_bpm_x100.store(bpm_x100);
    g_a_time_num.store(num);
    g_a_time_den.store(den);
    if (chord) {
        std::strncpy(g_a_chord, chord, sizeof(g_a_chord) - 1);
        g_a_chord[sizeof(g_a_chord) - 1] = '\0';
    }
}

void notify_per_note(uint8_t note, float pb_phase_signed) {
    g_b_note.store(note);
    int16_t q = (int16_t)(pb_phase_signed * 32767.0f);
    g_b_pb_phase_q15.store(q);
}

void notify_resolution(uint8_t note, uint16_t vel16, uint32_t cc32, uint32_t pb32) {
    g_c_note.store(note);
    g_c_vel16.store(vel16);
    g_c_cc32.store(cc32);
    g_c_pb32.store(pb32);
    g_c_step.fetch_add(1);
}

void notify_program(uint8_t program, uint8_t msb, uint8_t lsb) {
    g_d_program.store(program);
    g_d_bank_msb.store(msb);
    g_d_bank_lsb.store(lsb);
}

void notify_rpn(RpnVariant v, uint8_t msb, uint8_t lsb, int64_t value_or_delta) {
    uint8_t bit = 1u << (uint8_t)v;
    g_e_flags.fetch_or(bit);
    static const char* tags[] = {"RPN", "NRPN", "Rel RPN", "Rel NRPN"};
    std::snprintf(g_e_last_label, sizeof(g_e_last_label),
                  "%s %02X/%02X val %lld",
                  tags[(uint8_t)v], (unsigned)msb, (unsigned)lsb,
                  (long long)value_or_delta);
}

void notify_attribute(uint8_t note, int16_t cents) {
    g_f_note.store(note);
    g_f_cents.store(cents);
}

void notify_sysex7(const uint8_t* bytes, size_t count) {
    uint8_t n = (uint8_t)(count > 16 ? 16 : count);
    for (uint8_t i = 0; i < n; ++i) g_g_bytes[i] = bytes[i];
    g_g_count.store(n);
}

void notify_dctpq(uint16_t tpq, uint32_t delta) {
    g_h_tpq.store(tpq);
    g_h_delta.store(delta);
}

void notify_pe(const char* property, uint8_t subscribers) {
    if (property) {
        std::strncpy(g_i_property, property, sizeof(g_i_property) - 1);
        g_i_property[sizeof(g_i_property) - 1] = '\0';
    }
    g_i_subscribers.store(subscribers);
}

void notify_end_of_clip() {
    g_j_started_ms.store((uint32_t)(time_us_64() / 1000ULL));
    g_j_fade.store(255);
}

}  // namespace scene_display

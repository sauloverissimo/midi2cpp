/*
 * piano_display.cpp, ST7789 piano roll for the LilyGo T-Display S3.
 *
 * Ported from the Arduino IDE example T-Display-S3-Piano (ESP32_Host_MIDI
 * portfolio) into a self-contained ESP-IDF component:
 *   - Arduino API removed (Serial, pinMode, digitalWrite); LovyanGFX
 *     handles backlight via Light_PWM, ESP_LOGx replaces Serial.
 *   - Music theory analysis (chord names, intervals, formulas) dropped:
 *     the recipe is a USB MIDI 2.0 device receiver showcase, not a
 *     theory tool. Info bar carries identity + counters instead.
 *   - Public API tightened to the four functions the recipe actually
 *     calls from the FreeRTOS tasks: init, set_note_active, set_status,
 *     bump_counter, render_frame.
 *
 * Display config: ST7789 1.9" 320x170 IPS, parallel 8-bit on GPIO39..48
 * (D0..D7) + GP8 WR + GP9 RD + GP6 CS + GP7 RS/DC + GP5 RST + GP38
 * backlight (PWM, channel 7). Identical to the upstream T-Display S3
 * Arduino driver; LovyanGFX configures it 1:1.
 */
#include "piano_display.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace piano_display {

namespace {

constexpr const char* TAG = "piano-display";

// ── Screen layout (landscape, 320x170 after rotation) ────────────────────────
constexpr int SCREEN_W  = 320;
constexpr int SCREEN_H  = 170;
constexpr int INFO_H    = 48;
constexpr int PIANO_Y   = INFO_H;
constexpr int PIANO_H   = SCREEN_H - INFO_H;   // 122 px

// ── Piano key geometry, 25 keys (C..C, 15 white) ─────────────────────────────
constexpr int KEYS_SPAN    = 25;
constexpr int WHITE_KEYS   = 15;
constexpr int WHITE_KEY_W  = 21;
constexpr int WHITE_KEY_H  = PIANO_H;
constexpr int BLACK_KEY_W  = 12;
constexpr int BLACK_KEY_H  = static_cast<int>(PIANO_H * 0.60f);
constexpr int PIANO_X      = (SCREEN_W - WHITE_KEYS * WHITE_KEY_W) / 2;

constexpr int VIEW_DEFAULT = 48;   // C3 (typical 25-key controller range)
constexpr int VIEW_MIN     = 0;
constexpr int VIEW_MAX     = 103;

// ── Colours (RGB565) ─────────────────────────────────────────────────────────
constexpr uint16_t COL_WHITE_NORMAL  = 0xFFFF;
constexpr uint16_t COL_WHITE_ACTIVE  = 0x07FF;   // cyan
constexpr uint16_t COL_BLACK_NORMAL  = 0x0841;
constexpr uint16_t COL_BLACK_ACTIVE  = 0xFBE0;   // warm orange
constexpr uint16_t COL_KEY_BORDER    = 0x0000;
constexpr uint16_t COL_INFO_BG       = 0x1082;
constexpr uint16_t COL_HEADER_BG     = 0x2945;
constexpr uint16_t COL_DIVIDER       = 0x2945;
constexpr uint16_t COL_DIM           = 0x8410;
constexpr uint16_t COL_TEXT          = 0xFFFF;
constexpr uint16_t COL_ACCENT        = 0x07FF;
constexpr uint16_t COL_NOTE          = 0xFFE0;
constexpr uint16_t COL_OFF_BELOW     = 0xF800;   // red
constexpr uint16_t COL_OFF_ABOVE     = 0x001F;   // blue

// ── Semitone lookup ──────────────────────────────────────────────────────────
constexpr int SEMITONE_TO_WHITE[12]    = {0,-1,1,-1,2,3,-1,4,-1,5,-1,6};
constexpr int BLACK_LEFT_NEIGHBOR[12]  = {-1,0,-1,2,-1,-1,5,-1,7,-1,9,-1};
const char* const NOTE_NAMES[12]       = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// ── LovyanGFX driver tied to the T-Display S3 wiring ─────────────────────────
class TDisplayS3LCD : public lgfx::LGFX_Device {
public:
    TDisplayS3LCD() {
        {
            auto cfg = _bus.config();
            cfg.pin_wr = 8; cfg.pin_rd = 9; cfg.pin_rs = 7;
            cfg.pin_d0 = 39; cfg.pin_d1 = 40; cfg.pin_d2 = 41; cfg.pin_d3 = 42;
            cfg.pin_d4 = 45; cfg.pin_d5 = 46; cfg.pin_d6 = 47; cfg.pin_d7 = 48;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = 6;
            cfg.pin_rst  = 5;
            cfg.pin_busy = -1;
            cfg.offset_rotation = 1;
            cfg.offset_x = 35;
            cfg.readable    = false;
            cfg.invert      = true;
            cfg.rgb_order   = false;
            cfg.dlen_16bit  = false;
            cfg.bus_shared  = false;
            cfg.panel_width  = 170;
            cfg.panel_height = 320;
            _panel.config(cfg);
        }
        setPanel(&_panel);
        {
            auto cfg = _bl.config();
            cfg.pin_bl     = 38;
            cfg.invert     = false;
            cfg.freq       = 22000;
            cfg.pwm_channel = 7;
            _bl.config(cfg);
            _panel.setLight(&_bl);
        }
    }
private:
    lgfx::Bus_Parallel8 _bus;
    lgfx::Panel_ST7789  _panel;
    lgfx::Light_PWM     _bl;
};

// ── State ────────────────────────────────────────────────────────────────────
TDisplayS3LCD g_lcd;
LGFX_Sprite   g_sprite(&g_lcd);
bool          g_initialised = false;

// Active-note buffer. Plain bool[128]; byte writes are atomic on
// Xtensa, no need for std::atomic<bool>. The render task and the UMP
// callback task touch independent indices.
bool          g_active[128] = {};

int           g_view_start = VIEW_DEFAULT;

// Counters (atomic so the bump from any task is safe).
std::atomic<uint32_t> g_count_note_on  {0};
std::atomic<uint32_t> g_count_note_off {0};
std::atomic<uint32_t> g_count_cc       {0};
std::atomic<uint32_t> g_count_pb       {0};
std::atomic<uint32_t> g_count_other    {0};

// Status string (info bar). Short read/write; bounded copy.
char            g_status[32] = "init...";
SemaphoreHandle_t g_status_mutex = nullptr;

void auto_view_to(int midi) {
    int desired = (midi / 12) * 12 - 12;   // place note ~middle of view
    if (desired < VIEW_MIN) desired = VIEW_MIN;
    if (desired > VIEW_MAX) desired = VIEW_MAX;
    if (desired == g_view_start) return;
    ESP_LOGI(TAG, "auto-view %d -> %d (note %d)", g_view_start, desired, midi);
    g_view_start = desired;
}

void draw_piano(LGFX_Sprite& s) {
    s.fillRect(0, PIANO_Y, SCREEN_W, PIANO_H, COL_KEY_BORDER);

    // White keys first
    for (int n = g_view_start; n < g_view_start + KEYS_SPAN; n++) {
        int st = n % 12;
        if (SEMITONE_TO_WHITE[st] < 0) continue;
        int wi = ((n - g_view_start) / 12) * 7 + SEMITONE_TO_WHITE[st];
        if (wi < 0 || wi >= WHITE_KEYS) continue;
        int x = PIANO_X + wi * WHITE_KEY_W;
        uint16_t col = g_active[n] ? COL_WHITE_ACTIVE : COL_WHITE_NORMAL;
        s.fillRect(x, PIANO_Y + 1, WHITE_KEY_W - 1, WHITE_KEY_H - 2, col);
        s.drawFastHLine(x, PIANO_Y + WHITE_KEY_H - 2, WHITE_KEY_W - 1, COL_KEY_BORDER);
    }
    // Black keys on top
    for (int n = g_view_start; n < g_view_start + KEYS_SPAN; n++) {
        int st = n % 12;
        if (SEMITONE_TO_WHITE[st] >= 0) continue;
        int nbSt = BLACK_LEFT_NEIGHBOR[st];
        int nbNote = (n / 12) * 12 + nbSt;
        int nbWi = ((nbNote - g_view_start) / 12) * 7 + SEMITONE_TO_WHITE[nbSt];
        int x = PIANO_X + nbWi * WHITE_KEY_W + WHITE_KEY_W - BLACK_KEY_W / 2;
        uint16_t col = g_active[n] ? COL_BLACK_ACTIVE : COL_BLACK_NORMAL;
        s.fillRect(x, PIANO_Y + 1, BLACK_KEY_W, BLACK_KEY_H, col);
        s.drawRect(x, PIANO_Y + 1, BLACK_KEY_W, BLACK_KEY_H, COL_KEY_BORDER);
    }
}

void draw_info_bar(LGFX_Sprite& s) {
    s.fillRect(0, 0, SCREEN_W, INFO_H, COL_INFO_BG);
    s.drawFastHLine(0, INFO_H - 1, SCREEN_W, COL_HEADER_BG);

    // Top row: identity (left) + status (right)
    s.setFont(&fonts::Font2);
    s.setTextColor(COL_NOTE, COL_INFO_BG);
    s.drawString("TDisplayS3", 6, 4);

    s.setTextColor(COL_DIM, COL_INFO_BG);
    s.drawString("MIDI 2.0 RX", 6 + 80, 4);

    {
        char status_copy[32];
        if (g_status_mutex && xSemaphoreTake(g_status_mutex, 0) == pdTRUE) {
            std::strncpy(status_copy, g_status, sizeof(status_copy) - 1);
            status_copy[sizeof(status_copy) - 1] = '\0';
            xSemaphoreGive(g_status_mutex);
        } else {
            std::strncpy(status_copy, g_status, sizeof(status_copy) - 1);
            status_copy[sizeof(status_copy) - 1] = '\0';
        }
        s.setTextColor(COL_ACCENT, COL_INFO_BG);
        int tw = s.textWidth(status_copy);
        int x = SCREEN_W - tw - 6;
        if (x < 6 + 160) x = 6 + 160;
        s.drawString(status_copy, x, 4);
    }

    // Bottom row: counters
    s.setFont(&fonts::Font0);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "On %lu  Off %lu  CC %lu  PB %lu  Other %lu",
                  (unsigned long)g_count_note_on.load(),
                  (unsigned long)g_count_note_off.load(),
                  (unsigned long)g_count_cc.load(),
                  (unsigned long)g_count_pb.load(),
                  (unsigned long)g_count_other.load());
    s.setTextColor(COL_TEXT, COL_INFO_BG);
    s.drawString(buf, 6, 28);

    // View range label (top-right under status)
    int viewEnd = g_view_start + KEYS_SPAN - 1;
    char range[32];
    std::snprintf(range, sizeof(range), "%s%d-%s%d",
                  NOTE_NAMES[g_view_start % 12], (g_view_start / 12) - 1,
                  NOTE_NAMES[viewEnd       % 12], (viewEnd       / 12) - 1);
    s.setFont(&fonts::Font0);
    s.setTextColor(COL_DIM, COL_INFO_BG);
    int rw = s.textWidth(range);
    s.drawString(range, SCREEN_W - rw - 6, 36);

    // Out-of-view indicators
    bool below = false, above = false;
    for (int n = 0; n < 128; n++) {
        if (!g_active[n]) continue;
        if (n < g_view_start)              below = true;
        if (n >= g_view_start + KEYS_SPAN) above = true;
    }
    if (below) {
        s.fillRoundRect(0, 18, 14, 14, 3, COL_OFF_BELOW);
        s.setFont(&fonts::Font0);
        s.setTextColor(COL_TEXT, COL_OFF_BELOW);
        s.drawString("<", 3, 21);
    }
    if (above) {
        s.fillRoundRect(SCREEN_W - 14, 18, 14, 14, 3, COL_OFF_ABOVE);
        s.setFont(&fonts::Font0);
        s.setTextColor(COL_TEXT, COL_OFF_ABOVE);
        s.drawString(">", SCREEN_W - 11, 21);
    }
}

}  // namespace

void init() {
    if (g_initialised) return;

    // T-Display S3 has a dedicated power-enable pin for the LCD VDD
    // rail (GP15). Without driving it HIGH the panel stays dark even
    // when the backlight PWM is up. The pin is independent of the
    // LovyanGFX panel/bus config; we toggle it here before the LCD
    // init starts.
    gpio_config_t pwr_cfg = {};
    pwr_cfg.pin_bit_mask = (1ULL << 15);
    pwr_cfg.mode         = GPIO_MODE_OUTPUT;
    pwr_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    pwr_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    pwr_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&pwr_cfg);
    gpio_set_level(GPIO_NUM_15, 1);

    g_lcd.init();
    g_lcd.setRotation(2);
    g_lcd.setBrightness(255);
    g_lcd.fillScreen(0x0000);

    g_sprite.setColorDepth(16);
    g_sprite.setPsram(true);
    if (!g_sprite.createSprite(SCREEN_W, SCREEN_H)) {
        ESP_LOGE(TAG, "sprite alloc failed (320x170 16bpp = ~108 KB), check PSRAM config");
        return;
    }
    g_sprite.setTextDatum(lgfx::top_left);

    g_status_mutex = xSemaphoreCreateMutex();

    g_initialised = true;
    ESP_LOGI(TAG, "ST7789 320x170 + sprite ready");
}

void set_note_active(uint8_t note, bool active) {
    if (note >= 128) return;
    g_active[note] = active;

    if (active) {
        if (note < g_view_start || note >= g_view_start + KEYS_SPAN) {
            auto_view_to(note);
        }
    }
}

void bump_counter(Counter c) {
    switch (c) {
        case Counter::NoteOn:    g_count_note_on.fetch_add(1, std::memory_order_relaxed); break;
        case Counter::NoteOff:   g_count_note_off.fetch_add(1, std::memory_order_relaxed); break;
        case Counter::CC:        g_count_cc.fetch_add(1, std::memory_order_relaxed); break;
        case Counter::PitchBend: g_count_pb.fetch_add(1, std::memory_order_relaxed); break;
        case Counter::Other:     g_count_other.fetch_add(1, std::memory_order_relaxed); break;
    }
}

void set_status(const char* text) {
    if (text == nullptr) return;
    if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        std::strncpy(g_status, text, sizeof(g_status) - 1);
        g_status[sizeof(g_status) - 1] = '\0';
        xSemaphoreGive(g_status_mutex);
    }
}

void render_frame() {
    if (!g_initialised) return;
    draw_piano(g_sprite);
    draw_info_bar(g_sprite);
    g_lcd.startWrite();
    g_sprite.pushSprite(0, 0);
    g_lcd.endWrite();
}

}  // namespace piano_display

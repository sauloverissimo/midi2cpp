/*
 * main.cpp: adafruit-feather-rp2040-host-midi2-showcase
 *
 * USB MIDI 2.0 host on the Adafruit Feather RP2040 USB Host. Receives
 * UMP from any MIDI 2.0 device plugged into the USB-A port (PIO-USB
 * on GP16/GP17), decodes it via m2host's typed callbacks, and renders
 * the device topology + live UMP stream on a 128x64 SSD1306 OLED
 * connected over I2C1 (STEMMA QT, GP2/GP3).
 *
 * Visible behaviour:
 *   1. Splash + spinner while waiting for a device
 *   2. On device mount: "Connecting…" + identity lines as they arrive
 *      (UMP Stream Endpoint Discovery + MIDI-CI Discovery Inquiry are
 *      auto-fired by m2host)
 *   3. Live: each inbound UMP gets a colored log line on the OLED
 *      (NoteOn green, NoteOff dim, CC blue, Pitch Bend magenta, etc.)
 *      and a per-device note counter in the status bar
 *   4. On unmount: "Disconnected" red, return to spinner
 */
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "bsp/board_api.h"

#include "feather_host.h"
#include "display.h"

using namespace midi2;

// ----------------------------------------------------------------------------
// Note name helper
// ----------------------------------------------------------------------------
static const char* kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static void note_name(uint8_t pitch, char* buf, size_t len) {
    int octave = (pitch / 12) - 1;
    std::snprintf(buf, len, "%s%d", kNoteNames[pitch % 12], octave);
}

// ----------------------------------------------------------------------------
// Per-device note counter (one per connected idx). Monotonic, increments
// on every NoteOn, never decremented on NoteOff. Surfaced in the status
// bar as a quick "is this thing still alive" indicator.
// ----------------------------------------------------------------------------
static uint32_t g_notes_pressed[Host::MAX_DEVICES] = {0};

#if HOST_STRESS
// Stress verdict state. The flood device emits MIDI 2.0 note-ons whose 16-bit
// velocity is a monotonic sequence, contiguous on the wire. Any gap in the
// received sequence is a lost packet, anywhere between the device and the
// host's dispatch. We accumulate during the burst and print only at idle (a
// print mid-burst would stall the RX path and fake a loss).
static uint32_t g_st_recv       = 0;
static uint32_t g_st_gaps       = 0;
static uint16_t g_st_expected   = 0;
static bool     g_st_started    = false;
static bool     g_st_reported   = false;
static uint32_t g_st_last_rx_ms = 0;
#endif

// ----------------------------------------------------------------------------
// Display colors (16-bit RGB565). The SSD1306 is monochrome, non-zero
// is "lit". The colour value is preserved in the API as a hint for any
// future colour OLED port.
// ----------------------------------------------------------------------------
constexpr uint16_t COLOR_NOTE_ON   = 0x07E0;  // green
constexpr uint16_t COLOR_NOTE_OFF  = 0x8410;  // dim
constexpr uint16_t COLOR_CC        = 0x001F;  // blue
constexpr uint16_t COLOR_PROG      = 0xFFE0;  // yellow
constexpr uint16_t COLOR_PB        = 0xF81F;  // magenta
constexpr uint16_t COLOR_PRESSURE  = 0xFC10;  // pink
constexpr uint16_t COLOR_PER_NOTE  = 0x07FF;  // cyan
constexpr uint16_t COLOR_HEARTBEAT = 0x4208;  // very dim
constexpr uint16_t COLOR_INFO      = 0x07E0;  // green
constexpr uint16_t COLOR_WARN      = 0xF800;  // red

// ----------------------------------------------------------------------------
// m2host callback wiring, every event becomes a coloured log line.
// ----------------------------------------------------------------------------
static void install_callbacks(m2host& midi) {
    midi.onDeviceConnected([](uint8_t idx, const m2host::DeviceIdentity& id) {
        // protocolVersion is the bcdMSC reported by the device (e.g.
        // 0x0100 for MIDI 1.0, 0x0200 for MIDI 2.0). Use the >= 0x0200
        // check rather than truthiness, both values are non-zero.
        const char* proto = (id.protocolVersion >= 0x0200) ? "MIDI 2.0"
                                                            : "MIDI 1.0";
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] %s", (unsigned)idx, proto);
        display_log(line, COLOR_INFO);
        display_status("Connecting...");
        if (idx < Host::MAX_DEVICES) g_notes_pressed[idx] = 0;
    });

    midi.onIdentityUpdated([](uint8_t idx, const m2host::DeviceIdentity& id) {
        char line[32];
        // Pick the most informative line we can: prefer endpoint name,
        // fall back to manufacturer.family.model triple. Always
        // idx-prefixed so multi-device topologies stay readable.
        if (id.endpointName[0]) {
            std::snprintf(line, sizeof(line), "[%u] name: %s",
                           (unsigned)idx, id.endpointName);
        } else if (id.familyId || id.modelId) {
            std::snprintf(line, sizeof(line),
                           "[%u] %02X%02X%02X f%04X m%04X",
                           (unsigned)idx,
                           id.manufacturerId[0], id.manufacturerId[1],
                           id.manufacturerId[2], id.familyId, id.modelId);
        } else {
            return;
        }
        display_log(line, COLOR_INFO);
    });

    midi.onDeviceDisconnected([](uint8_t idx) {
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] disconnected", (unsigned)idx);
        display_log(line, COLOR_WARN);
        display_status("Waiting...");
        if (idx < Host::MAX_DEVICES) g_notes_pressed[idx] = 0;
    });

    // MIDI 2.0 Channel Voice
#if HOST_STRESS
    // Stress mode: the only inbound traffic is the flood. Check the sequence
    // (velocity) for contiguity; never touch the display here (it would stall
    // the RX path mid-burst). The verdict is printed at idle in the loop.
    midi.onNoteOn([](uint8_t /*idx*/, uint8_t /*ch*/, uint8_t /*note*/, uint16_t vel) {
        const uint16_t seq = vel;
        if (!g_st_started) { g_st_started = true; g_st_expected = seq; }
        if (seq != g_st_expected) g_st_gaps += (uint16_t)(seq - g_st_expected);
        g_st_expected   = (uint16_t)(seq + 1);
        g_st_reported   = false;
        g_st_last_rx_ms = (uint32_t)(time_us_64() / 1000ULL);
        ++g_st_recv;
    });
#else
    midi.onNoteOn([](uint8_t idx, uint8_t ch, uint8_t note, uint16_t vel) {
        char nn[8]; note_name(note, nn, sizeof(nn));
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] On %s ch%u v%04X",
                       (unsigned)idx, nn, (unsigned)ch, (unsigned)vel);
        display_log(line, COLOR_NOTE_ON);
        if (idx < Host::MAX_DEVICES) ++g_notes_pressed[idx];
    });
#endif
    midi.onNoteOff([](uint8_t idx, uint8_t ch, uint8_t note, uint16_t /*vel*/) {
        char nn[8]; note_name(note, nn, sizeof(nn));
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] Off %s ch%u",
                       (unsigned)idx, nn, (unsigned)ch);
        display_log(line, COLOR_NOTE_OFF);
    });
    midi.onCC([](uint8_t idx, uint8_t ch, uint8_t cc, uint32_t v) {
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] CC%u ch%u %08lX",
                       (unsigned)idx, (unsigned)cc, (unsigned)ch,
                       (unsigned long)v);
        display_log(line, COLOR_CC);
    });
    midi.onPitchBend([](uint8_t idx, uint8_t ch, uint32_t v) {
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] PB ch%u %08lX",
                       (unsigned)idx, (unsigned)ch, (unsigned long)v);
        display_log(line, COLOR_PB);
    });
    midi.onChannelPressure([](uint8_t idx, uint8_t ch, uint32_t v) {
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] ChP ch%u %08lX",
                       (unsigned)idx, (unsigned)ch, (unsigned long)v);
        display_log(line, COLOR_PRESSURE);
    });
    midi.onPolyPressure([](uint8_t idx, uint8_t ch, uint8_t note, uint32_t v) {
        char nn[8]; note_name(note, nn, sizeof(nn));
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] PolyP %s %08lX",
                       (unsigned)idx, nn, (unsigned long)v);
        display_log(line, COLOR_PRESSURE);
    });
    midi.onPerNotePitchBend([](uint8_t idx, uint8_t /*g*/, uint8_t ch,
                                uint8_t note, uint32_t v) {
        char nn[8]; note_name(note, nn, sizeof(nn));
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] PNPB %s ch%u %08lX",
                       (unsigned)idx, nn, (unsigned)ch, (unsigned long)v);
        display_log(line, COLOR_PER_NOTE);
    });
    midi.onJRTimestamp([](uint8_t idx, uint8_t /*g*/, uint16_t ts) {
        char line[32];
        std::snprintf(line, sizeof(line), "[%u] JR-TS %04X",
                       (unsigned)idx, (unsigned)ts);
        display_log(line, COLOR_HEARTBEAT);
    });
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
int main() {
    stdio_init_all();

    // Power up the USB-A 5V gate FIRST so the upstream device has time
    // to boot and pull up before tusb_init starts polling for it. The
    // 1.5s splash below provides the settle window the TinyUSB host
    // driver needs to observe a stable bus.
    feather_host::power_on_usb_a();

    sleep_ms(200);
    display_init();
    sleep_ms(1500);

    m2host midi;
    install_callbacks(midi);

    feather_host::init(midi);

    display_live_begin();
    display_status("Waiting...");

    uint32_t last_status_ms = 0;
    bool any_mounted_prev = false;

    while (true) {
        feather_host::task(midi);

        uint32_t now = (uint32_t)(time_us_64() / 1000ULL);

#if HOST_STRESS
        // Print the verdict once the flood goes quiet (>300 ms without RX).
        // Nothing is printed mid-burst; the display is skipped entirely so the
        // RX path stays short. recv = packets received; gaps = sequence holes
        // (lost packets); rxDropped = m2host ring overflow. All zero = no loss.
        if (g_st_started && !g_st_reported && (now - g_st_last_rx_ms) > 300) {
            g_st_reported = true;
            const uint32_t dropped = midi.rxDropped();
            const bool ok = (g_st_gaps == 0) && (dropped == 0);
            std::printf("RESULT recv=%lu gaps=%lu rxDropped=%lu => %s\r\n",
                        (unsigned long)g_st_recv, (unsigned long)g_st_gaps,
                        (unsigned long)dropped, ok ? "100% OK" : "FAILED");
        }
        continue;   // headless stress: UART verdict only, no display work
#endif

        bool any_mounted = (midi.deviceCount() > 0);

        if (!any_mounted) {
            if (now - last_status_ms > 200) {
                last_status_ms = now;
                display_connecting(now);
            }
        } else {
            // First mount transition: switch from spinner to live view
            // and force the next status-bar update on this tick instead
            // of waiting up to 2 s for the periodic refresh.
            if (!any_mounted_prev) {
                display_live_begin();
                last_status_ms = 0;
            }

            if (now - last_status_ms > 2000) {
                last_status_ms = now;
                uint32_t total_notes = 0;
                for (uint8_t i = 0; i < Host::MAX_DEVICES; ++i) {
                    total_notes += g_notes_pressed[i];
                }
                char line[24];
                std::snprintf(line, sizeof(line), "n=%u devs=%u",
                               (unsigned)total_notes,
                               (unsigned)midi.deviceCount());
                display_status(line);
            }
        }
        any_mounted_prev = any_mounted;
    }
}

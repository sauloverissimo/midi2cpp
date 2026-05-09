/*
 * midi2cpp HelloMIDI2 example, Arduino sketch.
 *
 * Minimal demo that builds the m2device + m2ci pair, wires the four
 * platform hooks (write, now, mount/alt, RNG), and prints to Serial
 * when inbound UMP fires the dispatch path. Compiles on any
 * Arduino-compatible board with C++17 support; the no-op write hook
 * keeps the sketch self-contained so it builds without a USB MIDI 2.0
 * transport on the wire.
 *
 * To get a real device, replace plat_write with the USB MIDI 2.0
 * transport for the board:
 *   - Teensy native:  usbMIDI.sendUMP(...)
 *   - TinyUSB:        tud_midi_n_stream_write(...)
 *   - ESP32_Host_MIDI USBMIDI2Connection: connection.write(...)
 *
 * Reference platform recipes (Pico SDK, ESP-IDF, TinyUSB CMake,
 * PlatformIO) live under midi2cpp/examples/ — see the README's
 * Boards table.
 */

#include <midi2cpp.h>

using namespace midi2;

static m2device midi;
static m2ci     ci(midi);

// 1. Outbound UMP. Real platforms forward to USB MIDI write here.
static size_t plat_write(const uint32_t* words, size_t count) {
    (void)words;
    return count;
}

// 2. Monotonic ms clock used by the JR Timestamp heartbeat.
static uint32_t plat_now() { return millis(); }

// 3. Entropy source for MUID. Caller picks any non-zero source.
static uint32_t plat_rng() { return random(0x7FFFFFFF); }

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    Serial.println("=== midi2cpp HelloMIDI2 ===");

    midi.setWriteFn(plat_write);
    midi.setNowFn(plat_now);
    midi.setMounted(true);
    midi.setAltSetting(1);          // 1 = MIDI 2.0 stream
    midi.begin();
    midi.enableJRHeartbeat(500);

    ci.setRngFn(plat_rng);
    static const uint8_t mfrId[3] = {0x7D, 0x00, 0x00};   // educational prefix
    ci.begin(mfrId, /*family*/ 0x0001, /*model*/ 0x0001, /*version*/ 0x00010000);

    // Inbound NoteOn -> Serial.
    midi.onNoteOn([](uint8_t /*g*/, uint8_t ch, uint8_t n, uint16_t v,
                     uint8_t /*at*/, uint16_t /*ad*/) {
        Serial.print("NoteOn ch=");
        Serial.print(ch);
        Serial.print(" note=");
        Serial.print(n);
        Serial.print(" vel16=0x");
        Serial.println(v, HEX);
    });

    // Synthesize a NoteOn locally to exercise the dispatch path.
    Serial.println("Sending C4 vel 0xC000 to its own dispatcher...");
    midi.noteOn(0, 60, 0xC000);
}

void loop() {
    // Real platforms drain inbound UMP into midi.feedRx(words, count) here.
    midi.task();   // dispatches reassembled SysEx, fires JR heartbeat
    delay(10);
}

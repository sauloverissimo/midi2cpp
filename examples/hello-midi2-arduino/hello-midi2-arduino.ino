/*
 * hello-midi2-arduino: minimal midi2cpp Arduino sketch.
 *
 * Builds the m2device + m2ci pair with the platform hooks (write, clock,
 * RNG) and the standard MIDI-CI responder package, then loops one NoteOn
 * through its own dispatcher. No USB transport is wired: plat_write is a
 * no-op, so the sketch compiles on any Arduino board with C++17.
 *
 * The MIDI-CI bootstrap below is the Workbench-validated baseline. Keep
 * it when adapting this sketch to a real transport:
 *   - Teensy native:  usbMIDI.sendUMP(...) in plat_write
 *   - TinyUSB:        tud_midi2_n_ump_write(...) in plat_write
 *   - ESP32_Host_MIDI: connection.write(...)
 * Hardware recipes per board live under midi2cpp/examples/.
 */

#include <midi2cpp.h>

using namespace midi2;

static m2device midi;
static m2ci     ci(midi);

/* Identity: keep the ci.begin bytes and the DeviceInfo JSON in sync.
 * 0x7D is the educational SysEx prefix; version 0x00010000 encodes to
 * versionId [0,0,4,0] on the wire (7 bits per byte). */
static const uint8_t kMfrId[3]   = {0x7D, 0x00, 0x00};
static const uint8_t kProfile[5] = {0x7E, 0x00, 0x00, 0x01, 0x00};  // GM 1

static const char kDeviceInfo[] =
    "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[18,0],"
     "\"versionId\":[0,0,4,0],\"manufacturer\":\"midi2.diy\","
     "\"family\":\"Arduino\",\"model\":\"Hello MIDI 2.0\","
     "\"version\":\"0.0.1\"}";
static const char kChannelList[] = "[{\"title\":\"Main\",\"channel\":1}]";
static const char kProgramList[] = "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]";

// Outbound UMP. Wire the board's USB MIDI 2.0 transport here.
static size_t plat_write(const uint32_t* words, size_t count) {
    (void)words;
    return count;
}

static uint32_t plat_now() { return millis(); }
static uint32_t plat_rng() { return random(0x7FFFFFFF); }

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    Serial.println("=== midi2cpp hello-midi2-arduino ===");

    midi.setWriteFn(plat_write);
    midi.setNowFn(plat_now);
    midi.setMounted(true);
    midi.setAltSetting(1);          // 1 = MIDI 2.0 stream
    midi.begin();
    midi.enableJRHeartbeat(500);

    /* MIDI-CI responder: Discovery + Profile + Property Exchange +
     * Process Inquiry, answered by the library. These four resources are
     * what a MIDI 2.0 Workbench session fetches. */
    ci.setRngFn(plat_rng);
    ci.begin(kMfrId, /*family*/ 0x0001, /*model*/ 0x0012,
             /*version*/ 0x00010000);
    ci.addProfile(kProfile, /*alwaysOn*/ false);
    ci.addPropertyStatic("DeviceInfo",  kDeviceInfo);
    ci.addPropertyStatic("ChannelList", kChannelList);
    ci.addPropertyStatic("ProgramList", kProgramList);
    ci.setMidiReport(0x01, 0x00000000FFFFFFFFull,
                     0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull);

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

    // Loop one NoteOn through the dispatcher to prove the path.
    Serial.println("Sending C4 vel 0xC000 to its own dispatcher...");
    midi.noteOn(0, 60, 0xC000);
}

void loop() {
    // Real transports drain inbound UMP into midi.feedRx(words, count) here.
    midi.task();   // dispatches reassembled SysEx, fires the JR heartbeat
    delay(10);
}

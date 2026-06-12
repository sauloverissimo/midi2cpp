// midi2cpp / teensy41-host-midi2
// USB MIDI 2.0 host showcase on Teensy 4.1.
//
// Requires:
//   - Teensyduino 1.60+.
//   - Arduino IDE > Tools > USB Type > Serial (debug output via
//     Serial Monitor; device-side MIDI is intentionally not enabled
//     here, this recipe is pure host).
//   - USBHost_t36 fork sauloverissimo/USBHost_t36 branch
//     feature/midi2-host-base overlaid onto your Teensyduino install.
//   - midi2cpp Arduino library (sauloverissimo/midi2cpp); the midi2
//     core is bundled, no separate midi2 library needed.
//
// Hardware:
//   - Teensy 4.1.
//   - USB Host cable plugged into the Teensy 4.1 USB host port
//     (5-pin header next to the USB device connector). See
//     https://www.pjrc.com/store/cable_usb_host_t36.html
//   - One USB MIDI 2.0 device connected to the USB-A side
//     (Daisy Seed, RP2040 Pico, Teensy device, etc.).
//   - Teensy USB device port plugged into the development host for
//     debug print via Serial Monitor at 115200.
//
// What it does:
//   On every detected mount, the host issues UMP Stream Endpoint
//   Discovery + MIDI-CI Discovery Inquiry to the peer device. As the
//   peer responds the local Host identity cache fills in (endpoint
//   name, function blocks, MIDI-CI manufacturer/family/model). All
//   inbound UMP traffic is decoded by the typed callbacks below and
//   printed to Serial one line per message.

#include "src/teensy41_host_midi2.h"

// Zero-loss stress receiver, the pair of the UMP test bench flood
// (rp2040-promicro-ump-test-bench built with -DBENCH_AUTOFLOOD=ON).
// Set to 1 and re-upload: NoteOn/NoteOff stop printing per message
// (a print mid-burst would stall the RX path and fake a loss) and a
// sequence verdict is printed once the flood goes idle. Same shape as
// HOST_STRESS in adafruit-feather-rp2040-host-midi2.
#define HOST_STRESS 0

midi2::Host midi;

#if HOST_STRESS
// The flood device emits MIDI 2.0 note-ons whose 16-bit velocity is a
// monotonic sequence, contiguous on the wire. Any gap in the received
// sequence is a lost packet, anywhere between the device and this
// sketch's dispatch.
static uint32_t g_st_recv       = 0;
static uint32_t g_st_gaps       = 0;
static uint16_t g_st_expected   = 0;
static bool     g_st_started    = false;
static bool     g_st_reported   = false;
static uint32_t g_st_last_rx_ms = 0;
#endif

static void registerRxHandlers(midi2::Host& m);

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { }
#if HOST_STRESS
    Serial.println(F("[host-stress] ready, waiting for the flood"));
#else
    Serial.println(F("[teensy41-host-midi2] boot"));
#endif

    teensy41_host::init(midi);
    registerRxHandlers(midi);
}

void loop() {
    teensy41_host::task(midi);

#if HOST_STRESS
    // Verdict only at idle: 1 s without traffic ends the measurement
    // window. The counters keep accumulating if the flood resumes.
    if (g_st_started && !g_st_reported &&
        (millis() - g_st_last_rx_ms) > 1000) {
        Serial.print(F("[host-stress] received="));
        Serial.print(g_st_recv);
        Serial.print(F(" gaps="));
        Serial.print(g_st_gaps);
        Serial.println(g_st_gaps == 0 ? F(" verdict=ZERO-LOSS")
                                      : F(" verdict=LOSS"));
        g_st_reported = true;
    }
#endif
}

// -- RX handlers --------------------------------------------------------

static void registerRxHandlers(midi2::Host& m) {
    m.onDeviceConnected([](uint8_t idx, const midi2::Host::DeviceIdentity& id) {
        const char* proto = (id.protocolVersion >= 0x02) ? "MIDI 2.0" : "MIDI 1.0";
        Serial.print(F("[host] connected idx="));
        Serial.print(idx);
        Serial.print(F(" "));
        Serial.print(proto);
        Serial.print(F(" bcdMSC=0x"));
        Serial.println(id.bcdMSC, HEX);
    });
    m.onDeviceDisconnected([](uint8_t idx) {
        Serial.print(F("[host] disconnected idx="));
        Serial.println(idx);
    });
    m.onIdentityUpdated([](uint8_t idx, const midi2::Host::DeviceIdentity& id) {
        Serial.print(F("[host] identity idx="));
        Serial.print(idx);
        if (id.endpointName[0]) {
            Serial.print(F(" name="));
            Serial.print(id.endpointName);
        }
        if (id.familyId || id.modelId) {
            Serial.print(F(" family=0x"));
            Serial.print(id.familyId, HEX);
            Serial.print(F(" model=0x"));
            Serial.print(id.modelId, HEX);
        }
        Serial.println();
    });

#if HOST_STRESS
    // Stress mode: count and check the sequence, never print per RX.
    m.onNoteOn([](uint8_t /*idx*/, uint8_t /*g*/, uint8_t /*ch*/,
                  uint8_t /*n*/, uint16_t v,
                  uint8_t /*at*/, uint16_t /*ad*/) {
        const uint16_t seq = v;
        if (!g_st_started) { g_st_started = true; g_st_expected = seq; }
        if (seq != g_st_expected) g_st_gaps += (uint16_t)(seq - g_st_expected);
        g_st_expected   = (uint16_t)(seq + 1);
        g_st_reported   = false;
        g_st_last_rx_ms = millis();
        ++g_st_recv;
    });
    m.onNoteOff([](uint8_t /*idx*/, uint8_t /*g*/, uint8_t /*ch*/,
                   uint8_t /*n*/, uint16_t /*v*/,
                   uint8_t /*at*/, uint16_t /*ad*/) {
        // Flood note-offs arrive in the same burst; stay silent.
    });
#else
    m.onNoteOn([](uint8_t idx, uint8_t g, uint8_t ch,
                  uint8_t n, uint16_t v,
                  uint8_t /*at*/, uint16_t /*ad*/) {
        Serial.print(F("[rx] NoteOn idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" ch=")); Serial.print(ch);
        Serial.print(F(" note=")); Serial.print(n);
        Serial.print(F(" vel16=0x")); Serial.println(v, HEX);
    });
    m.onNoteOff([](uint8_t idx, uint8_t g, uint8_t ch,
                   uint8_t n, uint16_t v,
                   uint8_t /*at*/, uint16_t /*ad*/) {
        Serial.print(F("[rx] NoteOff idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" ch=")); Serial.print(ch);
        Serial.print(F(" note=")); Serial.print(n);
        Serial.print(F(" vel16=0x")); Serial.println(v, HEX);
    });
#endif
    m.onCC([](uint8_t idx, uint8_t g, uint8_t ch,
              uint8_t cc, uint32_t val) {
        Serial.print(F("[rx] CC idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" ch=")); Serial.print(ch);
        Serial.print(F(" cc=")); Serial.print(cc);
        Serial.print(F(" val32=0x")); Serial.println(val, HEX);
    });
    m.onPitchBend([](uint8_t idx, uint8_t g, uint8_t ch, uint32_t val) {
        Serial.print(F("[rx] PitchBend idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" ch=")); Serial.print(ch);
        Serial.print(F(" val32=0x")); Serial.println(val, HEX);
    });
    m.onChannelPressure([](uint8_t idx, uint8_t g, uint8_t ch, uint32_t val) {
        Serial.print(F("[rx] ChannelPressure idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" ch=")); Serial.print(ch);
        Serial.print(F(" val32=0x")); Serial.println(val, HEX);
    });
    m.onProgram([](uint8_t idx, uint8_t g, uint8_t ch,
                   uint8_t prog, uint8_t /*bv*/,
                   uint8_t /*msb*/, uint8_t /*lsb*/) {
        Serial.print(F("[rx] Program idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" ch=")); Serial.print(ch);
        Serial.print(F(" prog=")); Serial.println(prog);
    });
    m.onPerNotePitchBend([](uint8_t idx, uint8_t g, uint8_t ch,
                            uint8_t n, uint32_t val) {
        Serial.print(F("[rx] PerNotePB idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" ch=")); Serial.print(ch);
        Serial.print(F(" note=")); Serial.print(n);
        Serial.print(F(" val32=0x")); Serial.println(val, HEX);
    });
    m.onTempo([](uint8_t idx, uint8_t g, uint32_t tenNsPerQn) {
        Serial.print(F("[rx] Tempo idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" 10ns/qn=")); Serial.println(tenNsPerQn);
    });
    m.onTimeSignature([](uint8_t idx, uint8_t g, uint8_t num, uint8_t denom) {
        Serial.print(F("[rx] TimeSig idx=")); Serial.print(idx);
        Serial.print(F(" g=")); Serial.print(g);
        Serial.print(F(" ")); Serial.print(num);
        Serial.print(F("/")); Serial.println(1 << denom);
    });
}

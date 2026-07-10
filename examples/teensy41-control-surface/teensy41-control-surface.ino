// midi2cpp / teensy41-control-surface
// USB MIDI 2.0 hardware-driven control surface on Teensy 4.1.
//
// Requires:
//   - Teensyduino with the cores fork sauloverissimo/cores
//     branch feature/usb-midi2-descriptors applied to teensy4/.
//   - Arduino IDE > Tools > USB Type > MIDI2
//   - midi2cpp Arduino library (sauloverissimo/midi2cpp); the midi2
//     core is bundled, no separate midi2 library needed.
//
// Hardware:
//   - 4 x 10k linear pots on A0..A3 (CTR pin to A0..A3, ends to 3V3 / GND)
//   - 4 x momentary switches on D2..D5 (one side to GPIO, other to GND;
//     internal pull-up enabled, switch reads LOW when pressed)
//   - Teensy USB device port to host (DAW, Linux ALSA, Windows MIDI
//     Services, macOS Audio MIDI Setup)
//
// USB identity inherits VID/PID 0x16C0:0x0485 from the cores fork;
// Manufacturer "midi2.diy" and Product "Teensy41 CS"
// come from src/usb_names_override.c via the cores' weak-alias hook.
// UMP Stream identity programmed below.

#include <usb_midi2.h>
#include "src/teensy41_control_surface.h"

teensy41::Backend backend;
static midi2::m2ci ci(backend.device());

// MIDI-CI identity (matches the Discovery Reply bytes).
static const uint8_t  kMfrId[3]  = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId  = 0x0001;
static const uint16_t kModelId   = 0x0002;
static const uint32_t kVersion   = 0x00010000;
static const char     kDeviceInfo[] =
    "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[2,0],\"versionId\":[0,0,4,0],\"manufacturer\":\"midi2.diy\","
    "\"family\":\"Teensy\","
    "\"model\":\"Teensy41 Control Surface MIDI 2.0\","
    "\"version\":\"0.0.1\"}";
static const char     kChannelList[] =
    "[{\"title\":\"Control Surface\",\"channel\":1}]";

// -- pots ---------------------------------------------------------------
static const uint8_t  kPotPin[4]   = { A0, A1, A2, A3 };
static const uint8_t  kPotCC[4]    = { 1, 74, 71, 91 }; // Modulation, Brightness, Resonance, Reverb Send
static uint16_t       pot_smooth[4]   = { 0 };  // EMA in 12-bit space
static uint16_t       pot_last_tx[4]  = { 0 };  // last value sent

static const uint16_t kPotDeadband = 32;        // ~0.8% of 12-bit range

// -- switches -----------------------------------------------------------
static const uint8_t  kSwPin[4]    = { 2, 3, 4, 5 };
static const uint8_t  kSwNote[4]   = { 60, 61, 62, 63 }; // C4, C#4, D4, D#4
static bool           sw_state[4]  = { false }; // debounced (true = pressed)
static bool           sw_raw[4]    = { false };
static uint32_t       sw_change_ms[4] = { 0 };

static const uint32_t kSwDebounceMs = 20;

// -- RX ----------------------------------------------------------------
static void printRx(const __FlashStringHelper *tag, uint8_t g, uint8_t ch);
static void registerRxHandlers(midi2::Device &midi);


// Boot MUID entropy: cycle counter + ADC noise (no TRNG on i.MX RT1062).
static uint32_t plat_rng() {
    uint32_t s = micros();
    for (int i = 0; i < 8; ++i) {
        s = (s << 3) ^ analogRead(A0) ^ ARM_DWT_CYCCNT;
    }
    return s;
}

void setup()
{
	Serial.begin(115200);
	analogReadResolution(12);

	for (uint8_t i = 0; i < 4; i++) {
		pinMode(kSwPin[i], INPUT_PULLUP);
		pot_smooth[i]  = analogRead(kPotPin[i]); // seed so first CC reflects pot position
		pot_last_tx[i] = pot_smooth[i];
	}

	backend.begin();
	auto &midi = backend.device();

	// UMP Stream identity (M2-104-UM Endpoint Discovery)
	midi.sendEndpointInfo(/*umpVerMajor*/1, /*umpVerMinor*/1,
	                      /*staticFb*/true, /*numFb*/1,
	                      /*midi2*/true, /*midi1*/true,
	                      /*rxJr*/false, /*txJr*/true);
	midi.sendDeviceIdentity(kMfrId, kFamilyId, kModelId, kVersion);
	midi.sendEndpointNameUpdate("Teensy41 CS");
	midi.sendProductInstanceIdUpdate("Teensy41-controlsurface-0001");
	midi.sendFbInfo(/*active*/true, /*fbNum*/0,
	                /*direction*/3 /*Bidirectional*/,
	                /*uiHint*/3   /*Bidirectional*/,
	                /*firstGroup*/0, /*numGroups*/1,
	                /*midiCiVer*/0, /*sysex8*/false, /*protocol*/3);
	midi.sendFbNameUpdate(0, "Control Surface");

	// MIDI-CI: identity + PE resources (same package as teensy41-midi2)
	ci.setRngFn(plat_rng);
	ci.begin(kMfrId, kFamilyId, kModelId, kVersion);
	ci.addPropertyStatic("DeviceInfo",  kDeviceInfo);
	ci.addPropertyStatic("ChannelList", kChannelList);
	ci.setPropertySubscribable("ChannelList", true);
	ci.addPropertyStatic("ProgramList", "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]");

	registerRxHandlers(midi);
}

void loop()
{
	backend.task();

	auto &midi = backend.device();
	uint32_t now = millis();

	// pots: EMA smoothing + deadband, send CC 32-bit when moved
	for (uint8_t i = 0; i < 4; i++) {
		uint16_t raw = (uint16_t)analogRead(kPotPin[i]);          // 0..4095
		pot_smooth[i] += ((int32_t)raw - (int32_t)pot_smooth[i]) >> 3;
		uint16_t v = pot_smooth[i];
		uint16_t delta = (v > pot_last_tx[i]) ? (v - pot_last_tx[i])
		                                     : (pot_last_tx[i] - v);
		if (delta >= kPotDeadband || (v == 4095 && pot_last_tx[i] != 4095)
		                          || (v == 0    && pot_last_tx[i] != 0)) {
			pot_last_tx[i] = v;
			// 12-bit ADC -> 32-bit CC: shift left 20, fill low bits with MSB replica
			uint32_t cc32 = ((uint32_t)v << 20) | ((uint32_t)v << 8) | ((uint32_t)v >> 4);
			midi.sendCC(/*group*/0, /*channel*/0, kPotCC[i], cc32);
		}
	}

	// switches: millis-based debounce, send NoteOn/Off on edge
	for (uint8_t i = 0; i < 4; i++) {
		bool pressed = (digitalRead(kSwPin[i]) == LOW);
		if (pressed != sw_raw[i]) {
			sw_raw[i] = pressed;
			sw_change_ms[i] = now;
		}
		if (sw_raw[i] != sw_state[i] &&
		    (now - sw_change_ms[i]) >= kSwDebounceMs) {
			sw_state[i] = sw_raw[i];
			if (sw_state[i]) {
				midi.sendNoteOn(/*group*/0, /*channel*/0, kSwNote[i], 0xC000);
			} else {
				midi.sendNoteOff(/*group*/0, /*channel*/0, kSwNote[i], 0x4000);
			}
		}
	}
}

// -- RX handlers --------------------------------------------------------
static void printRx(const __FlashStringHelper *tag, uint8_t g, uint8_t ch)
{
	Serial.print(F("[RX] ")); Serial.print(tag);
	Serial.print(F(" g=")); Serial.print(g);
	Serial.print(F(" ch=")); Serial.print(ch);
}

static void registerRxHandlers(midi2::Device &midi)
{
	midi.onNoteOn([](uint8_t g, uint8_t ch, uint8_t n, uint16_t v,
	                 uint8_t /*at*/, uint16_t /*ad*/) {
		printRx(F("NoteOn"), g, ch);
		Serial.print(F(" note=")); Serial.print(n);
		Serial.print(F(" vel16=0x")); Serial.println(v, HEX);
	});
	midi.onNoteOff([](uint8_t g, uint8_t ch, uint8_t n, uint16_t v,
	                  uint8_t /*at*/, uint16_t /*ad*/) {
		printRx(F("NoteOff"), g, ch);
		Serial.print(F(" note=")); Serial.print(n);
		Serial.print(F(" vel16=0x")); Serial.println(v, HEX);
	});
	midi.onCC([](uint8_t g, uint8_t ch, uint8_t idx, uint32_t val) {
		printRx(F("CC"), g, ch);
		Serial.print(F(" idx=")); Serial.print(idx);
		Serial.print(F(" val32=0x")); Serial.println(val, HEX);
	});
	midi.onPitchBend([](uint8_t g, uint8_t ch, uint32_t val) {
		printRx(F("PitchBend"), g, ch);
		Serial.print(F(" val32=0x")); Serial.println(val, HEX);
	});
	midi.onChannelPressure([](uint8_t g, uint8_t ch, uint32_t val) {
		printRx(F("ChannelPressure"), g, ch);
		Serial.print(F(" val32=0x")); Serial.println(val, HEX);
	});
	midi.onProgram([](uint8_t g, uint8_t ch, uint8_t prog,
	                  uint8_t /*bv*/, uint8_t /*msb*/, uint8_t /*lsb*/) {
		printRx(F("Program"), g, ch);
		Serial.print(F(" prog=")); Serial.println(prog);
	});
	midi.onPerNotePitchBend([](uint8_t g, uint8_t ch, uint8_t n, uint32_t val) {
		printRx(F("PerNotePB"), g, ch);
		Serial.print(F(" note=")); Serial.print(n);
		Serial.print(F(" val32=0x")); Serial.println(val, HEX);
	});
	midi.onTempo([](uint8_t g, uint32_t tenNsPerQn) {
		Serial.print(F("[RX] Tempo g=")); Serial.print(g);
		Serial.print(F(" 10ns/qn=")); Serial.println(tenNsPerQn);
	});
	midi.onTimeSignature([](uint8_t g, uint8_t num, uint8_t denom) {
		Serial.print(F("[RX] TimeSig g=")); Serial.print(g);
		Serial.print(F(" ")); Serial.print(num);
		Serial.print(F("/")); Serial.println(1 << denom);
	});
}

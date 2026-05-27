// midi2cpp / teensy41-midi2
// USB MIDI 2.0 device showcase on Teensy 4.1.
//
// Requires:
//   - Teensyduino with the cores fork sauloverissimo/cores
//     branch feature/usb-midi2-descriptors applied to teensy4/.
//   - Arduino IDE > Tools > USB Type > MIDI2
//   - midi2cpp Arduino library (sauloverissimo/midi2cpp).
//   - midi2 Arduino library (sauloverissimo/midi2), pulled in by
//     midi2cpp.
//
// USB identity: VID 0x16C0, PID 0x0485 (PJRC's USB_TYPE = MIDI2 slot,
// kept intact); Manufacturer "github.com/sauloverissimo" and Product
// "Teensy41" come from src/usb_names_override.c via the cores' official
// weak-alias hook. UMP Stream identity programmed below: Endpoint Name
// "Teensy41", FB 0 "Main", Product Instance ID "Teensy41-showcase-0001".

#include <usb_midi2.h>
#include "src/teensy41_midi2.h"

teensy41::Backend backend;
midi2::m2ci       ci(backend.device());

static uint32_t last_demo = 0;
static uint8_t  demo_note = 60;

static const uint8_t  kMfrId[3]    = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId    = 0x0001;
static const uint16_t kModelId     = 0x0001;
static const uint32_t kVersion     = 0x00010000;
static const uint8_t  kProfileGm[5] = {0x7E, 0x00, 0x00, 0x01, 0x00};
static const char     kDeviceInfo[] =
    "{\"manufacturer\":\"github.com/sauloverissimo\","
    "\"family\":\"Teensy 4.x\","
    "\"model\":\"Teensy 4.1\","
    "\"version\":\"1.0.0\"}";
static const char     kChannelList[] =
    "{\"channelList\":[{\"title\":\"Main\",\"channel\":1}]}";

void setup()
{
	backend.begin();
	auto& midi = backend.device();

	// UMP Stream identity (M2-104-UM Endpoint Discovery)
	midi.sendEndpointInfo(/*umpVerMajor*/1, /*umpVerMinor*/1,
	                      /*staticFb*/true, /*numFb*/1,
	                      /*midi2*/true, /*midi1*/true,
	                      /*rxJr*/false, /*txJr*/true);
	midi.sendDeviceIdentity(kMfrId, kFamilyId, kModelId, kVersion);
	midi.sendEndpointNameUpdate("Teensy41");
	midi.sendProductInstanceIdUpdate("Teensy41-showcase-0001");
	midi.sendFbInfo(/*active*/true, /*fbNum*/0,
	                /*direction*/3 /*Bidirectional*/,
	                /*uiHint*/3   /*Bidirectional*/,
	                /*firstGroup*/0, /*numGroups*/1,
	                /*midiCiVer*/0, /*sysex8*/false, /*protocol*/3);
	midi.sendFbNameUpdate(0, "Main");

	// MIDI-CI: identity + profile + 2 properties
	ci.begin(kMfrId, kFamilyId, kModelId, kVersion);
	ci.addProfile(kProfileGm);
	ci.addPropertyStatic("DeviceInfo",  kDeviceInfo);
	ci.addPropertyStatic("ChannelList", kChannelList);
	ci.setPropertySubscribable("ChannelList", true);
}

void loop()
{
	backend.task();

	uint32_t now = millis();
	if (now - last_demo >= 5000) {
		auto& midi = backend.device();

		// 32-bit velocity NoteOn (MIDI 2.0)
		midi.sendNoteOn(/*group*/0, /*channel*/0, demo_note, 0xC000);
		delay(200);
		midi.sendNoteOff(/*group*/0, /*channel*/0, demo_note, 0x8000);

		// 32-bit CC sweep + pitch bend + channel pressure + program change
		midi.sendCC(0, 0, 1,  0x80000000);
		midi.sendCC(0, 0, 74, 0xFFFFFFFF);
		midi.sendPitchBend(0, 0, 0xC0000000);
		midi.sendChannelPressure(0, 0, 0x60000000);
		midi.sendProgram(0, 0, 42);

		// Per-note expression (MIDI 2.0 exclusive)
		midi.sendPerNotePitchBend(0, 0, demo_note, 0x90000000);
		midi.sendRegPerNoteController(0, 0, demo_note, 7 /*Volume*/, 0xFFFFFFFF);

		// Flex Data
		midi.sendTempo(0, 50000000);    // 120 BPM in 10-ns ticks per quarter
		midi.sendTimeSignature(0, 4, 2); // 4/4

		demo_note = (demo_note >= 72) ? 60 : (uint8_t)(demo_note + 1);
		last_demo = now;
	}
}

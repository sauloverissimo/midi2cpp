// midi2cpp / daisyseed-midi2
// USB MIDI 2.0 device showcase on the Daisy Seed (STM32H750, Cortex-M7).
//
// Requires the libDaisy fork sauloverissimo/libDaisy branch
// feat/usb-midi2-transport (USB MIDI 2.0 descriptors + raw UMP I/O on
// MidiUsbTransport) plus the midi2cpp and midi2 sources. See README.
//
// USB identity: VID 0x0483, PID 0x5740 (STMicro / Daisy Seed defaults,
// kept intact), Product string "Daisy Seed MIDI 2.0" from the fork.
// UMP Stream identity programmed below: Endpoint Name "DaisySeed",
// FB 0 "Main", Product Instance ID "DaisySeed-showcase-0001".

#include "daisy_seed.h"
#include "daisyseed_midi2.h"

static daisy::DaisySeed   hw;
static daisyseed::Backend backend;
static midi2::m2ci        ci(backend.device());

static const uint8_t  kMfrId[3]     = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId     = 0x0001;
static const uint16_t kModelId      = 0x0001;
static const uint32_t kVersion      = 0x00010000;
static const uint8_t  kProfileGm[5] = {0x7E, 0x00, 0x00, 0x01, 0x00};
static const char     kDeviceInfo[] =
    "{\"manufacturer\":\"github.com/sauloverissimo\","
    "\"family\":\"Daisy Seed\","
    "\"model\":\"Daisy Seed\","
    "\"version\":\"1.0.0\"}";
static const char     kChannelList[] =
    "{\"channelList\":[{\"title\":\"Main\",\"channel\":1}]}";

int main(void) {
    hw.Init();
    backend.begin();
    auto& midi = backend.device();

    // UMP Stream identity (M2-104-UM Endpoint Discovery)
    midi.sendEndpointInfo(/*umpVerMajor*/ 1, /*umpVerMinor*/ 1,
                          /*staticFb*/ true, /*numFb*/ 1,
                          /*midi2*/ true, /*midi1*/ true,
                          /*rxJr*/ false, /*txJr*/ true);
    midi.sendDeviceIdentity(kMfrId, kFamilyId, kModelId, kVersion);
    midi.sendEndpointNameUpdate("DaisySeed");
    midi.sendProductInstanceIdUpdate("DaisySeed-showcase-0001");
    midi.sendFbInfo(/*active*/ true, /*fbNum*/ 0,
                    /*direction*/ 3 /*Bidirectional*/,
                    /*uiHint*/ 3 /*Bidirectional*/,
                    /*firstGroup*/ 0, /*numGroups*/ 1,
                    /*midiCiVer*/ 0, /*sysex8*/ false, /*protocol*/ 3);
    midi.sendFbNameUpdate(0, "Main");

    // MIDI-CI: identity + profile + 2 properties
    ci.begin(kMfrId, kFamilyId, kModelId, kVersion);
    ci.addProfile(kProfileGm);
    ci.addPropertyStatic("DeviceInfo", kDeviceInfo);
    ci.addPropertyStatic("ChannelList", kChannelList);
    ci.setPropertySubscribable("ChannelList", true);

    uint32_t last_demo = 0;
    uint8_t  demo_note = 60;

    for (;;) {
        backend.task();

        uint32_t now = daisy::System::GetNow();
        if (now - last_demo >= 5000) {
            // 16-bit velocity NoteOn (MIDI 2.0), 200 ms sustain
            midi.sendNoteOn(/*group*/ 0, /*channel*/ 0, demo_note, 0xC000);
            daisy::System::Delay(200);
            midi.sendNoteOff(/*group*/ 0, /*channel*/ 0, demo_note, 0x8000);

            // 32-bit CC sweep + pitch bend + channel pressure + program change
            midi.sendCC(0, 0, 1, 0x80000000);
            midi.sendCC(0, 0, 74, 0xFFFFFFFF);
            midi.sendPitchBend(0, 0, 0xC0000000);
            midi.sendChannelPressure(0, 0, 0x60000000);
            midi.sendProgram(0, 0, 42);

            // Per-note expression (MIDI 2.0 exclusive)
            midi.sendPerNotePitchBend(0, 0, demo_note, 0x90000000);
            midi.sendRegPerNoteController(0, 0, demo_note, 7 /*Volume*/,
                                          0xFFFFFFFF);

            // Flex Data
            midi.sendTempo(0, 50000000);     // 120 BPM in 10-ns ticks per quarter
            midi.sendTimeSignature(0, 4, 2); // 4/4

            demo_note = (demo_note >= 72) ? 60 : (uint8_t)(demo_note + 1);
            last_demo = now;
        }
    }
}

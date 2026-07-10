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

// DBG-RX7 instrumentation (temporary): device RX diagnostics from libDaisy,
// exfiltrated over MIDI TX (CC 110-114 on channel 15) so the Workbench can
// read them. Remove once the RX bug is pinpointed.
extern "C" {
extern volatile uint32_t usbd_dbg_dataout_count;  // OUT packets reaching USB class
extern volatile uint32_t usbd_dbg_rxcb_count;     // ReceiveCallback invocations
extern volatile uint32_t usbd_dbg_last_rxlen;     // last RxLength
extern volatile uint8_t  usbd_dbg_last_alt;       // usbd_midi2_alt_setting at DataOut
extern volatile uint8_t  usbd_dbg_rxactive;       // RxActive() at last callback
extern volatile uint32_t usbd_dbg_cdcrecv_count;  // CDC_Receive_FS invocations
extern volatile uint8_t  usbd_dbg_cb_is_dummy;    // rx_callback_fs == dummy?
extern volatile uint32_t usbd_dbg_preprecv_count; // USBD_LL_PrepareReceive (OUT re-arm) calls
extern volatile uint8_t  usbd_dbg_preprecv_status;// last HAL_PCD_EP_Receive status (0/1/2/3)
}

static daisy::DaisySeed   hw;
static daisyseed::Backend backend;
static midi2::m2ci        ci(backend.device());

static const uint8_t  kMfrId[3]     = {0x7D, 0x00, 0x00};
static const uint16_t kFamilyId     = 0x0001;
static const uint16_t kModelId      = 0x0003;
static const uint32_t kVersion      = 0x00010000;
static const uint8_t  kProfileGm[5] = {0x7E, 0x00, 0x00, 0x01, 0x00};
static const char     kDeviceInfo[] =
    "{\"manufacturerId\":[125,0,0],\"familyId\":[1,0],\"modelId\":[3,0],\"versionId\":[0,0,4,0],\"manufacturer\":\"midi2.diy\","
    "\"family\":\"Daisy\","
    "\"model\":\"Daisy Seed MIDI 2.0\","
    "\"version\":\"0.0.1\"}";
static const char     kChannelList[] =
    "[{\"title\":\"Main\",\"channel\":1}]";

int main(void) {
    hw.Init();
    backend.begin();
    auto& midi = backend.device();

    // UMP Stream responder (M2-104-UM Endpoint/FB Discovery). libDaisy has no
    // built-in Stream responder, so the app must answer the host's discovery
    // requests; an unanswered discovery leaves the endpoint info at defaults
    // and initiators skip MIDI-CI entirely.
    midi.onEndpointDiscovery([&midi](uint8_t filter) {
        if (filter & 0x01)
            midi.sendEndpointInfo(/*umpVerMajor*/ 1, /*umpVerMinor*/ 1,
                                  /*staticFb*/ true, /*numFb*/ 1,
                                  /*midi2*/ true, /*midi1*/ true,
                                  /*rxJr*/ false, /*txJr*/ true);
        if (filter & 0x02) midi.sendDeviceIdentity(kMfrId, kFamilyId, kModelId, kVersion);
        if (filter & 0x04) midi.sendEndpointNameUpdate("DaisySeed");
        if (filter & 0x08) midi.sendProductInstanceIdUpdate("DaisySeed-showcase-0001");
        if (filter & 0x10) midi.sendStreamConfigNotify(/*protocol*/ 0x02);
    });
    midi.onFbDiscovery([&midi](uint8_t fbNum, uint8_t filter) {
        uint8_t target = (fbNum == 0xFF) ? 0 : fbNum;
        if (target != 0) return;
        if (filter & 0x01)
            midi.sendFbInfo(/*active*/ true, /*fbNum*/ 0,
                            /*direction*/ 3 /*Bidirectional*/,
                            /*uiHint*/ 3 /*Bidirectional*/,
                            /*firstGroup*/ 0, /*numGroups*/ 1,
                            /*midiCiVer*/ 0x02, /*sysex8*/ true,
                            /*protocol*/ 0x02);
        if (filter & 0x02) midi.sendFbNameUpdate(0, "Main");
    });
    midi.onStreamConfigRequest([&midi](uint8_t protocol) {
        midi.sendStreamConfigNotify(protocol);
    });

    // Unsolicited identity at boot for hosts that captured enumeration early.
    midi.sendEndpointInfo(1, 1, true, 1, true, true, false, true);
    midi.sendDeviceIdentity(kMfrId, kFamilyId, kModelId, kVersion);
    midi.sendEndpointNameUpdate("DaisySeed");
    midi.sendProductInstanceIdUpdate("DaisySeed-showcase-0001");
    midi.sendFbInfo(true, 0, 3, 3, 0, 1, /*midiCiVer*/ 0x02, false, /*protocol*/ 0x02);
    midi.sendFbNameUpdate(0, "Main");

    // MIDI-CI: identity + profile + 2 properties
    // Boot MUID from the STM32H7 TRNG (initialized by System::Init).
    ci.setRngFn([]() -> uint32_t { return daisy::Random::GetValue(); });
    ci.begin(kMfrId, kFamilyId, kModelId, kVersion);
    ci.addProfile(kProfileGm);
    ci.addPropertyStatic("DeviceInfo", kDeviceInfo);
    ci.addPropertyStatic("ChannelList", kChannelList);
    ci.setPropertySubscribable("ChannelList", true);
    ci.addPropertyStatic("ProgramList", "[{\"title\":\"Default\",\"bankPC\":[0,0,0]}]");

    // Back the advertised Process Inquiry category with a MIDI report.
    ci.setMidiReport(0x01, 0x00000000FFFFFFFFull,
                     0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull);

    uint32_t last_demo = 0;
    uint32_t last_dbg  = 0;
    uint8_t  demo_note = 60;

    for (;;) {
        backend.task();

        uint32_t now = daisy::System::GetNow();

        // DBG-RX7: report device RX counters over MIDI (channel 15, CC 110-114)
        if (now - last_dbg >= 1000) {
            midi.sendCC(0, 15, 110, usbd_dbg_dataout_count);          // OUT reached class?
            midi.sendCC(0, 15, 111, (uint32_t)usbd_dbg_last_alt);     // alt (0/1/0xFF)
            midi.sendCC(0, 15, 112, usbd_dbg_rxcb_count);             // ReceiveCallback ran?
            midi.sendCC(0, 15, 113, (uint32_t)usbd_dbg_rxactive);     // RxActive (0/1/0xFF)
            midi.sendCC(0, 15, 114, usbd_dbg_last_rxlen);             // last RxLength
            midi.sendCC(0, 15, 115, usbd_dbg_cdcrecv_count);          // CDC_Receive_FS count
            midi.sendCC(0, 15, 116, (uint32_t)usbd_dbg_cb_is_dummy);  // callback == dummy?
            midi.sendCC(0, 15, 117, usbd_dbg_preprecv_count);         // OUT re-arm (PrepareReceive) count
            midi.sendCC(0, 15, 118, (uint32_t)usbd_dbg_preprecv_status);// last HAL EP_Receive status
            last_dbg = now;
        }

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

            // SysEx7 + SysEx8 + Mixed Data Set (single stream id, mfr 0x7D)
            static const uint8_t sx7[] = {0x7E, 0x7F, 0x06, 0x02, 0x7D, 0x01,
                                          0x00, 0x40, 0x00, 0x04, 0x00, 0x00};
            midi.sendSysEx7(0, sx7, sizeof sx7);
            static const uint8_t sx8[] = {0x7D, 0x01, 0x02, 0x03, 0x04};
            midi.sendSysEx8(0, /*streamId*/ 0, sx8, sizeof sx8);
            static const uint8_t mdsData[] = {0x7D, 0x4D, 0x44, 0x53};
            midi.sendMds(0, /*mdsId*/ 1, mdsData, sizeof mdsData, /*mfrId*/ 0x7D00);

            demo_note = (demo_note >= 72) ? 60 : (uint8_t)(demo_note + 1);
            last_demo = now;
        }
    }
}

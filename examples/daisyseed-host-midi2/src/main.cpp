// midi2cpp / daisyseed-host-midi2
// USB MIDI 2.0 host showcase on the Daisy Seed (STM32H750, Cortex-M7).
//
//   USB MIDI 2.0 device
//      |
//      v  Daisy USB-A jack (OTG_HS host, libDaisy fork transport)
//      |
//      v  MidiUsbTransport::Config::HOST  (raw UMP RX/TX)
//      |
//      v  midi2cpp m2host
//         * feedRx + task in the single-threaded main loop
//         * typed dispatch (NoteOn/Off, CC, PB, ChnPres, Program, Tempo)
//         * Endpoint Discovery + Identity tracking per device
//         * MIDI-CI Initiator (host MUID, Discovery Inquiry)
//      |
//      v  User callbacks (this file): printf to the Daisy log
//
// Requires the libDaisy fork sauloverissimo/libDaisy branch
// feat/usb-midi2-transport (USB MIDI 2.0 host path on MidiUsbTransport)
// plus the midi2cpp and midi2 sources. See README.
//
// The Daisy micro-USB connector is the device port, used here for power
// and the log (CDC virtual COM). USB host runs on the STM32H750 OTG_HS
// controller, exposed on the Seed edge connector (D29/D30 + USB ID).

#include "daisy_seed.h"
#include "daisyseed_host_midi2.h"

static daisy::DaisySeed        hw;
static daisyseed_host::Backend backend;

// Tracks whether a device is currently enumerated, so the idle liveness
// line reflects reality instead of always printing "waiting".
static bool s_device_present = false;

static void printIdentity(uint8_t idx, const midi2::Host::DeviceIdentity& id)
{
    hw.PrintLine("[id   ] dev=%u alt=%u bcdMSC=0x%04X ump=%u.%u fbs=%u",
                 idx, id.altSettingActive, id.bcdMSC,
                 id.umpVerMajor, id.umpVerMinor, id.numFunctionBlocks);
    hw.PrintLine("[id   ]   protocols m1=%s m2=%s",
                 id.supportsMidi1Protocol ? "yes" : "no",
                 id.supportsMidi2Protocol ? "yes" : "no");
    if(id.endpointName[0])
        hw.PrintLine("[id   ]   ep name  \"%s\"", id.endpointName);
    if(id.productInstanceId[0])
        hw.PrintLine("[id   ]   prod id  \"%s\"", id.productInstanceId);
    hw.PrintLine("[id   ]   manuf %02X %02X %02X family=0x%04X model=0x%04X",
                 id.manufacturerId[0], id.manufacturerId[1],
                 id.manufacturerId[2], id.familyId, id.modelId);
}

static void registerHandlers(midi2::Host& host)
{
    host.onDeviceConnected([](uint8_t idx,
                              const midi2::Host::DeviceIdentity& id) {
        s_device_present = true;
        hw.PrintLine("[conn ] dev=%u", idx);
        printIdentity(idx, id);
    });
    host.onDeviceDisconnected([](uint8_t idx) {
        s_device_present = false;
        hw.PrintLine("[disc ] dev=%u", idx);
    });
    host.onIdentityUpdated([](uint8_t idx,
                             const midi2::Host::DeviceIdentity& id) {
        printIdentity(idx, id);
    });

    host.onNoteOn([](uint8_t idx, uint8_t group, uint8_t channel,
                     uint8_t note, uint16_t velocity,
                     uint8_t /*attrType*/, uint16_t /*attrData*/) {
        hw.PrintLine("[on   ] dev=%u g=%u ch=%u note=%u vel=0x%04X",
                     idx, group, channel + 1, note, velocity);
    });
    host.onNoteOff([](uint8_t idx, uint8_t group, uint8_t channel,
                      uint8_t note, uint16_t velocity,
                      uint8_t /*attrType*/, uint16_t /*attrData*/) {
        hw.PrintLine("[off  ] dev=%u g=%u ch=%u note=%u vel=0x%04X",
                     idx, group, channel + 1, note, velocity);
    });
    host.onCC([](uint8_t idx, uint8_t group, uint8_t channel,
                 uint8_t index, uint32_t value) {
        hw.PrintLine("[cc%-3u] dev=%u g=%u ch=%u val=0x%08lX",
                     index, idx, group, channel + 1,
                     (unsigned long)value);
    });
    host.onPitchBend([](uint8_t idx, uint8_t group, uint8_t channel,
                        uint32_t value) {
        hw.PrintLine("[pb   ] dev=%u g=%u ch=%u val=0x%08lX",
                     idx, group, channel + 1, (unsigned long)value);
    });
    host.onChannelPressure([](uint8_t idx, uint8_t group, uint8_t channel,
                              uint32_t value) {
        hw.PrintLine("[chprs] dev=%u g=%u ch=%u val=0x%08lX",
                     idx, group, channel + 1, (unsigned long)value);
    });
    host.onProgram([](uint8_t idx, uint8_t group, uint8_t channel,
                      uint8_t program, uint8_t bMSB, uint8_t bLSB,
                      bool bankValid) {
        if(bankValid)
            hw.PrintLine("[prog ] dev=%u g=%u ch=%u prog=%u bank=%u/%u",
                         idx, group, channel + 1, program, bMSB, bLSB);
        else
            hw.PrintLine("[prog ] dev=%u g=%u ch=%u prog=%u",
                         idx, group, channel + 1, program);
    });
    host.onTempo([](uint8_t idx, uint8_t group, uint32_t tenNsPerQn) {
        // UMP Set Tempo carries 10 ns ticks per quarter note.
        // BPM = 60e9 ns per minute / (tenNsPerQn * 10 ns). Scale by 100
        // for two decimal places: 6e11 / tenNsPerQn.
        if(tenNsPerQn == 0)
            return;
        uint64_t bpm_x100 = (uint64_t)600000000000ULL / (uint64_t)tenNsPerQn;
        hw.PrintLine("[tempo] dev=%u g=%u %lu.%02lu bpm",
                     idx, group,
                     (unsigned long)(bpm_x100 / 100),
                     (unsigned long)(bpm_x100 % 100));
    });
}

int main(void)
{
    hw.Init();
    // Non-blocking: the host must run with or without a PC attached to
    // the log port. A device plugged into the USB-A jack is enumerated
    // regardless. Lines printed before the PC subscribes are dropped,
    // which is fine for a monitor.
    hw.StartLog(/*wait_for_pc*/ false);
    hw.PrintLine("midi2cpp daisyseed-host-midi2");
    hw.PrintLine("plug a USB MIDI 2.0 device into the USB-A jack");

    backend.begin();
    registerHandlers(backend.host());
    hw.PrintLine("host muid = 0x%07lX (ci initiator)",
                 (unsigned long)backend.host().hostMuid());

    uint32_t blink_time = 0;
    uint32_t alive_time = 0;
    bool     led_state  = false;

    for(;;)
    {
        backend.task();

        uint32_t now = daisy::System::GetNow();
        if(now >= blink_time)
        {
            hw.SetLed(led_state);
            led_state  = !led_state;
            blink_time = now + 500U;
        }
        // Liveness line every 5 s, so a log reader attaching after boot
        // sees the host is up and whether a device is enumerated.
        if(now >= alive_time)
        {
            hw.PrintLine("[alive] %lu ms, %s", (unsigned long)now,
                         s_device_present ? "device connected"
                                          : "waiting for device");
            alive_time = now + 5000U;
        }
    }
}

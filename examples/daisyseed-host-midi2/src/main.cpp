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

// Stress mode: pair of the UMP test bench flood (a device whose MIDI 2.0
// NoteOns carry a monotonic, contiguous 16-bit sequence in the velocity).
// Set to 1 and rebuild to turn the recipe into a zero-loss receiver: the
// per-message prints are replaced by an O(1) count + sequence check in the
// RX callback (a print in the hot path would stall the pipe and fake a
// loss), and a verdict prints once the flood goes idle. Same shape as
// HOST_STRESS in the Teensy and Feather host recipes.
#define HOST_STRESS 0

static daisy::DaisySeed        hw;
static daisyseed_host::Backend backend;

#if HOST_STRESS
// Answer key shared with the gabarito emitter: for a running 16-bit
// sequence seq, the emitter sends NoteOn(note, velocity=seq) then
// NoteOff(note, 0), where note = 48 + (seq % 24) (a chromatic walk over
// notes 48..71, so the flood carries varied tones, not one repeated
// note). The host confronts every received NoteOn against this key:
//   * velocity must be contiguous          -> no packet loss
//   * note must equal 48 + (velocity % 24)  -> no payload corruption
//   * NoteOff count must match NoteOn count -> pairs balanced, no stuck notes
// note = 48 + (seq % 24) is derived from the same seq on both sides, so
// it holds across the 16-bit wrap.
static const uint8_t  kStLowNote  = 48U;
static const uint8_t  kStNoteSpan = 24U;

static uint32_t g_st_on         = 0;
static uint32_t g_st_off        = 0;
static uint32_t g_st_gaps       = 0;
static uint32_t g_st_bad_note   = 0;
static uint16_t g_st_expected   = 0;
static bool     g_st_started    = false;
static uint32_t g_st_start_ms   = 0;
static bool     g_st_warmed     = false;
static uint32_t g_st_last_rx_ms = 0;
#else
// Tracks whether a device is currently enumerated, so the idle liveness
// line reflects reality instead of always printing "waiting".
static bool s_device_present = false;
#endif

#if !HOST_STRESS
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

#endif // !HOST_STRESS

static void registerHandlers(midi2::Host& host)
{
#if HOST_STRESS
    // Count and check the velocity sequence, never print per message (a
    // print in the RX hot path would stall the pipe and fake a loss).
    host.onNoteOn([](uint8_t /*idx*/, uint8_t /*g*/, uint8_t /*ch*/,
                     uint8_t note, uint16_t v,
                     uint8_t /*at*/, uint16_t /*ad*/) {
        const uint16_t seq = v;
        if(!g_st_started)
        {
            g_st_started  = true;
            g_st_expected = seq;
            g_st_start_ms = daisy::System::GetNow();
        }
        if(seq != g_st_expected)
            g_st_gaps += (uint16_t)(seq - g_st_expected);
        // Confront the tone against the answer key.
        if(note != (uint8_t)(kStLowNote + (seq % kStNoteSpan)))
            ++g_st_bad_note;
        g_st_expected   = (uint16_t)(seq + 1);
        g_st_last_rx_ms = daisy::System::GetNow();
        ++g_st_on;
    });
    // Every NoteOn is paired with a NoteOff; count them to confirm the
    // pairs stay balanced (no stuck notes) under the flood. Only count
    // once measurement has begun (on the first NoteOn), so a trailing
    // NoteOff caught before the first NoteOn does not skew the balance.
    host.onNoteOff([](uint8_t /*idx*/, uint8_t /*g*/, uint8_t /*ch*/,
                      uint8_t /*n*/, uint16_t /*v*/,
                      uint8_t /*at*/, uint16_t /*ad*/) {
        if(!g_st_started)
            return;
        g_st_last_rx_ms = daisy::System::GetNow();
        ++g_st_off;
    });
}
#else
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
#endif // HOST_STRESS

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
#if HOST_STRESS
    // Pure throughput receiver: disable auto-discovery so the CI Initiator
    // does not fire periodic Endpoint Discovery / MIDI-CI retries. Those
    // use a blocking send that would stall the receive path (and thus drop
    // inbound packets) while probing a device that is only flooding.
    backend.host().setAutoDiscover(false);
#endif
    registerHandlers(backend.host());
    hw.PrintLine("host muid = 0x%07lX (ci initiator)",
                 (unsigned long)backend.host().hostMuid());

    uint32_t blink_time = 0;
    bool     led_state  = false;
#if HOST_STRESS
    uint32_t report_time = 0;
#else
    uint32_t alive_time = 0;
#endif

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
#if HOST_STRESS
        // Warm-up: the first ~2 s after the flood begins overlap the host's
        // Endpoint Discovery / MIDI-CI handshake, whose brief blocking sends
        // can drop a few inbound packets. Clear the counters once so the
        // measurement window is pure steady state, then re-baseline on the
        // next NoteOn.
        if(g_st_started && !g_st_warmed
           && (now - g_st_start_ms) > 2000U)
        {
            g_st_on       = 0;
            g_st_off      = 0;
            g_st_gaps     = 0;
            g_st_bad_note = 0;
            g_st_started  = false;
            g_st_warmed   = true;
        }
        // Progress line every 2 s: running tally while the flood is live,
        // and the verdict once 1 s passes with no traffic. Pass criteria:
        // zero sequence gaps, no corrupted tones, balanced NoteOn/NoteOff.
        if(now >= report_time)
        {
            if(!g_st_started)
            {
                hw.PrintLine("[host-stress] waiting for flood");
            }
            else
            {
                const bool idle = (now - g_st_last_rx_ms) > 1000U;
                const bool pass = (g_st_gaps == 0) && (g_st_bad_note == 0)
                                  && (g_st_on == g_st_off);
                hw.PrintLine(
                    "[host-stress] on=%lu off=%lu gaps=%lu badnote=%lu %s",
                    (unsigned long)g_st_on, (unsigned long)g_st_off,
                    (unsigned long)g_st_gaps, (unsigned long)g_st_bad_note,
                    idle ? (pass ? "=> 100%-MATCH" : "=> MISMATCH")
                         : "(flooding)");
            }
            report_time = now + 2000U;
        }
#else
        // Liveness line every 5 s, so a log reader attaching after boot
        // sees the host is up and whether a device is enumerated. When a
        // device is present, the cached identity is reprinted: the
        // onDeviceConnected print fires at mount time, which a log reader
        // attaching later never sees, so echoing it here keeps the
        // endpoint name and function blocks (from Endpoint Discovery)
        // visible.
        if(now >= alive_time)
        {
            if(backend.host().isDeviceMounted(0))
            {
                hw.PrintLine("[alive] %lu ms, device connected",
                             (unsigned long)now);
                printIdentity(0, backend.host().identity(0));
            }
            else
            {
                hw.PrintLine("[alive] %lu ms, waiting for device",
                             (unsigned long)now);
            }
            alive_time = now + 5000U;
        }
#endif
    }
}

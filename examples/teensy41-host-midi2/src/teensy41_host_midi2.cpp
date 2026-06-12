// midi2cpp / teensy41-host-midi2: USBHost_t36 backend implementation.
//
// Owns the single USBHost instance, the single MIDIDevice_BigBuffer
// slot, the mount/unmount edge detector, and the bridge between
// USBHost_t36's raw UMP primitives (readUMP / writeUMP / umpMode) and
// midi2::Host's five public hooks.

#include "teensy41_host_midi2.h"
#include <USBHost_t36.h>

namespace teensy41_host {

namespace {

USBHost              myusb;
MIDIDevice_BigBuffer midi1(myusb);

midi2::Host* g_midi          = nullptr;
bool         g_was_connected = false;

// Outbound UMP, invoked by midi2::Host for every sendXxx and the JR
// heartbeat injection. Forwards to USBHost_t36 fork's writeUMP for the
// addressed device idx. Only idx 0 is wired; multi-device via hub is a
// future recipe extension (D-040 candidate).
void platform_write_fn(uint8_t idx, const uint32_t* words, size_t count) {
    if (idx != 0) return;
    if (!midi1.umpMode()) return;
    midi1.writeUMP(words, (uint8_t)count);
}

// Monotonic millisecond clock used by midi2::Host for CI Discovery
// timeout bookkeeping.
uint32_t platform_now_fn() {
    return (uint32_t)millis();
}

// Entropy source for the host's own MUID (CI Initiator role). The
// Cortex-M7 cycle counter on iMXRT1062 is non-deterministic at boot
// and adequate as a one-shot MUID seed.
uint32_t platform_rng_fn() {
    return ARM_DWT_CYCCNT;
}

} // namespace

void init(midi2::Host& midi) {
    g_midi = &midi;

    myusb.begin();

    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setRngFn(platform_rng_fn);
    midi.begin();
}

void task(midi2::Host& midi) {
    myusb.Task();

    // Edge-detected mount/unmount. USBHost_t36 fork exposes umpMode()
    // but not bcdMSC / protocol_version / cable_count; defaults below
    // describe a spec-v2 single-cable MIDI 2.0 peer. Auto-discovery
    // fires UMP Stream Endpoint Discovery + MIDI-CI Discovery Inquiry
    // shortly after notifyDeviceMounted, populating the real identity
    // from the peer's responses.
    bool now_connected = (midi1 && midi1.umpMode());
    if (now_connected != g_was_connected) {
        if (now_connected) {
            midi.notifyDeviceMounted(
                /*idx*/                0,
                /*protocolVersion*/    2,
                /*cableCount*/         1,
                /*altSettingActive*/   1,
                /*bcdMSC*/             0x0200);
        } else {
            midi.notifyDeviceUnmounted(0);
        }
        g_was_connected = now_connected;
    }

    // Drain inbound UMP. readUMP() returns 1 word-pair at a time;
    // feedRx queues into midi2::Host's SPSC ring, task() below drains
    // it and fires dispatch callbacks.
    if (now_connected) {
        uint32_t words[4];
        uint8_t  count = 0;
        while (midi1.readUMP(words, &count) > 0) {
            midi.feedRx(0, words, count);
        }
    }

    midi.task();
}

} // namespace teensy41_host

// midi2cpp / daisyseed-midi2: libDaisy backend implementation

#include "daisyseed_midi2.h"
#include "daisy_seed.h"
#include "hid/usb_midi.h"

extern "C" {
// Current MIDI 2.0 alternate setting, owned by the libDaisy fork USB stack.
// 0 = MIDI 1.0 (Alt 0), 1 = MIDI 2.0 UMP (Alt 1).
extern uint8_t usbd_midi2_alt_setting;
}

namespace daisyseed {

// The transport is callback-driven: the libDaisy USB MIDI middleware fires
// the receive callback, which assembles UMP words and hands them here.
static daisy::MidiUsbTransport s_transport;

// MIDI 1.0 (Alt 0) byte path is unused on this UMP device. StartRx still
// needs a parse callback to activate the receiver.
static void NoopParseCb(uint8_t*, size_t, void*) {}

static void UmpRxCb(const uint32_t* words, uint8_t count, void* ctx) {
    static_cast<Backend*>(ctx)->device().feedRx(words, count);
}

void Backend::begin(uint16_t jrHeartbeatMs) {
    daisy::MidiUsbTransport::Config cfg;
    cfg.periph = daisy::MidiUsbTransport::Config::INTERNAL;
    s_transport.Init(cfg);
    s_transport.SetUmpCallback(UmpRxCb, this);
    s_transport.StartRx(NoopParseCb, nullptr);

    dev_.setWriteFn([](const uint32_t* words, size_t count) {
        s_transport.Tx(reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(words)),
                       count * 4);
    });
    dev_.setNowFn([]() -> uint32_t { return daisy::System::GetNow(); });
    dev_.setMounted(true);
    dev_.begin();
    if (jrHeartbeatMs > 0) {
        dev_.enableJRHeartbeat(jrHeartbeatMs);
    }
}

void Backend::task() {
    dev_.setAltSetting(usbd_midi2_alt_setting);
    dev_.task();
}

} // namespace daisyseed

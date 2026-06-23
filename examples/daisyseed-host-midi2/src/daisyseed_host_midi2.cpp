// midi2cpp / daisyseed-host-midi2: libDaisy host backend implementation

#include "daisyseed_host_midi2.h"
#include "daisy_seed.h"
#include "hid/usb_host.h"
#include "hid/usb_midi.h"
#include "usbh_midi.h"  // USBH_MIDI_CLASS macro

namespace {

// The host transport is callback-driven: the libDaisy USB host middleware
// fires the class-active callback on enumeration, the receive callback as
// UMP words arrive. Both reference the single Backend instance.
daisy::USBHostHandle    s_usb_host;
daisy::MidiUsbTransport s_transport;
daisyseed_host::Backend* s_backend = nullptr;
bool s_transport_up = false;

// UMP from the plugged device. Feed it straight into midi2::Host (single
// producer: the libDaisy USB host runs in the main-loop context here, so
// feedRx and task() share one thread, satisfying the Host SPSC contract).
void UmpRxCb(const uint32_t* words, uint8_t count, void* /*ctx*/)
{
    if(s_backend)
        s_backend->host().feedRx(0, words, count);
}

// MIDI 1.0 byte path: a registered parse callback is required to activate
// StartRx. midi2::Host consumes UMP only, so MIDI 1.0 bytes are dropped
// here. A real MIDI 1.0 fallback would translate cable events to UMP MT
// 0x2 before feedRx; out of scope for this MIDI 2.0 host recipe.
void Midi1ParseCb(uint8_t*, size_t, void*) {}

// Outbound UMP (host to device): Endpoint Discovery, MIDI-CI inquiries,
// and any user sends. Tx picks UMP vs MIDI 1.0 framing from the active
// alt setting of the connected device.
void HostWriteFn(uint8_t /*idx*/, const uint32_t* words, size_t count)
{
    if(s_transport_up)
        s_transport.Tx(reinterpret_cast<uint8_t*>(
                            const_cast<uint32_t*>(words)),
                        count * 4);
}

void USBH_ClassActive(void* /*data*/)
{
    if(!s_usb_host.IsActiveClass(USBH_MIDI_CLASS))
        return;

    daisy::MidiUsbTransport::Config cfg;
    cfg.periph = daisy::MidiUsbTransport::Config::HOST;
    s_transport.Init(cfg);
    s_transport.SetUmpCallback(UmpRxCb, nullptr);
    s_transport.StartRx(Midi1ParseCb, nullptr);
    s_transport_up = true;

    // Announce the mount to midi2::Host. The host stack selects Alt 1
    // (MIDI 2.0 UMP, bcdMSC 0x0200) when the device exposes it; the real
    // protocol version and function block count are filled in by the
    // Endpoint Discovery that notifyDeviceMounted schedules.
    if(s_backend)
        s_backend->host().notifyDeviceMounted(0,
                                              /*protocolVersion*/ 2,
                                              /*cableCount*/ 1,
                                              /*altSettingActive*/ 1,
                                              /*bcdMSC*/ 0x0200);
}

void USBH_Disconnect(void* /*data*/)
{
    s_transport_up = false;
    if(s_backend)
        s_backend->host().notifyDeviceUnmounted(0);
}

void USBH_Connect(void* /*data*/) {}
void USBH_Error(void* /*data*/) {}

} // namespace

namespace daisyseed_host {

void Backend::begin()
{
    s_backend = this;

    host_.setWriteFn(HostWriteFn);
    host_.setNowFn([]() -> uint32_t { return daisy::System::GetNow(); });
    host_.setRngFn([]() -> uint32_t { return daisy::Random::GetValue(); });
    host_.begin();

    daisy::USBHostHandle::Config cfg;
    cfg.connect_callback      = USBH_Connect;
    cfg.disconnect_callback   = USBH_Disconnect;
    cfg.class_active_callback = USBH_ClassActive;
    cfg.error_callback        = USBH_Error;
    s_usb_host.Init(cfg);
    s_usb_host.RegisterClass(USBH_MIDI_CLASS);
}

void Backend::task()
{
    s_usb_host.Process();
    host_.task();
}

} // namespace daisyseed_host

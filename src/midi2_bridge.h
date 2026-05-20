// midi2_bridge.h, USB MIDI 2.0 multi-slot bridge.
//
// Bridge composes the three existing shapes (Device on the PC-facing
// side, Host on the upstream side, plus CI for MIDI Capability Inquiry)
// into a single object that exposes a multi-Function-Block topology to
// the host computer and forwards each upstream device's UMP into its
// own group window. Each upstream slot owns kGroupsPerSlot consecutive
// groups on the device-side endpoint; the bridge rewrites the group
// nibble of every forwarded UMP so the host PC sees a coherent view of
// up to kNumSlots devices in a single endpoint.
//
// Layout (kNumSlots = 4, kGroupsPerSlot = 4 default):
//
//   slot 0 -> Group 1..4   (Function Block 0)
//   slot 1 -> Group 5..8   (Function Block 1)
//   slot 2 -> Group 9..12  (Function Block 2)
//   slot 3 -> Group 13..16 (Function Block 3)
//
// Each Function Block carries a name pulled from the upstream
// Endpoint Name; an empty slot reports "(empty slot)".
//
// MIDI 1.0 alt 0 upstream devices are bridged via an internal
// ByteStreamConverter per slot; the bytes the platform feeds via
// feedHostMidi1Bytes() turn into MT 0x2 UMPs in the slot's first group.
//
// Platform contract:
//
//   Bridge::setDownstreamWriteFn(fn)         outbound UMP to PC
//   Bridge::setUpstreamWriteFn(fn)           outbound UMP to upstream device idx
//   Bridge::setNowFn(fn)                     monotonic ms clock (m2 Device + Host + CI)
//   Bridge::setRngFn(fn)                     entropy for CI MUIDs
//
//   Bridge::begin()                          starts m2 Device + Host + CI
//   Bridge::task()                           ticks Device + Host (call from main loop)
//
//   Bridge::feedDeviceRx(words, count)       drain RX from PC, iterate
//                                            UMP packets internally
//   Bridge::feedHostRx(idx, words, count)    drain RX from upstream MIDI 2.0
//   Bridge::feedHostMidi1Bytes(idx, b, n)    drain bytes from upstream MIDI 1.0
//
//   Bridge::slotSetActive(idx, active, alt)  called from upstream mount/unmount
//   Bridge::setDeviceMounted(bool)           PC-side mount mirror
//   Bridge::setDeviceAltSetting(uint8_t)     PC-side alt mirror
//
// The Stream Discovery responder is owned by the Bridge: the PC's
// Endpoint Discovery and Function Block Discovery messages flow in via
// feedDeviceRx and the Bridge replies with per-FB group windows + the
// dynamic FB Name for each active slot. This requires the underlying
// TinyUSB build to leave MT 0xF Stream messages in the RX FIFO instead
// of consuming them with the built-in responder; opt-in via
// CFG_TUD_MIDI2_USER_RESPONDER on the experiment/midi-coexistence
// branch.
//
// Threading: same single-task contract as m2 Device and m2 Host. Call
// begin() / task() / feed*() / slotSetActive() from one task only.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "midi2_device.h"
#include "midi2_ci.h"
#include "midi2_host.h"

namespace midi2 {

// kBridgeMaxSlots mirrors MIDI2CPP_HOST_MAX_DEVICES (the upstream slot
// limit Host already advertises). Bridges that want a smaller slot
// count still pay for the array; the tradeoff is symmetry with Host.
#ifndef MIDI2CPP_BRIDGE_MAX_SLOTS
#  define MIDI2CPP_BRIDGE_MAX_SLOTS MIDI2CPP_HOST_MAX_DEVICES
#endif

class Bridge {
public:
    static constexpr uint8_t MAX_SLOTS = MIDI2CPP_BRIDGE_MAX_SLOTS;

    Bridge();
    ~Bridge();

    Bridge(const Bridge&)            = delete;
    Bridge& operator=(const Bridge&) = delete;

    // Topology. Set before begin(); calls after begin() are silently
    // ignored. numSlots in 1..MAX_SLOTS, groupsPerSlot in 1..16, with
    // numSlots * groupsPerSlot <= 16 (USB-MIDI 2.0 group cap).
    void setNumSlots(uint8_t n);
    void setGroupsPerSlot(uint8_t n);
    uint8_t numSlots() const;
    uint8_t groupsPerSlot() const;

    // Composition accessors. Users wire callbacks (onNoteOn, etc.) on
    // the inner Device or Host directly; the Bridge owns the
    // Stream Discovery and CI Initiator surface itself.
    Device& device();
    CI&     ci();
    Host&   host();

    // Identity helpers. The Bridge stores them for later use during
    // Stream Discovery responses (Endpoint Info, Endpoint Name, Product
    // Instance ID, Device Identity, Stream Config Notify) and for
    // CI::begin(). All five are also forwarded to Device/CI getters
    // when the platform wants to reuse them.
    void setManufacturerId(const uint8_t mfrId[3]);
    void setFamily(uint16_t f);
    void setModel(uint16_t m);
    void setVersion(uint32_t v);
    void setEndpointName(const char* name);
    void setProductInstanceId(const char* id);

    // Platform contract.
    using DownstreamWriteFn = std::function<size_t(const uint32_t* words,
                                                    size_t count)>;
    using UpstreamWriteFn   = std::function<size_t(uint8_t idx,
                                                    const uint32_t* words,
                                                    size_t count)>;
    using NowFn             = std::function<uint32_t()>;
    using RngFn             = std::function<uint32_t()>;

    void setDownstreamWriteFn(DownstreamWriteFn fn);
    void setUpstreamWriteFn(UpstreamWriteFn fn);
    void setNowFn(NowFn fn);
    void setRngFn(RngFn fn);

    // Lifecycle. begin() wires the inner Device/Host/CI to the platform
    // hooks above, installs the multi-FB Stream Discovery responder,
    // and registers the host identity callbacks that drive slot naming.
    // task() ticks Device + Host once.
    void begin();
    void task();

    // ----- Slot lifecycle, called by the platform from upstream USB
    // mount / unmount events.
    //
    // alt = 0 means MIDI 1.0 byte stream (caller will feed via
    //          feedHostMidi1Bytes); alt = 1 means UMP (feedHostRx).
    //
    // For MIDI 2.0 upstream devices the platform should ALSO call
    // host().notifyDeviceMounted() so m2 Host's auto-discover sends
    // Endpoint Discovery + CI Discovery Inquiry. That path then fires
    // host().onIdentityUpdated(), which the Bridge intercepts to copy
    // the Endpoint Name into the slot and push an FB Name update.
    void slotSetActive(uint8_t idx, bool active, uint8_t alt);

    // ----- Raw RX feeders. Per-packet iteration and group rewrite are
    // internal to the Bridge.
    void feedDeviceRx(const uint32_t* words, size_t count);
    void feedHostRx(uint8_t idx, const uint32_t* words, size_t count);

    // USB-MIDI 1.0 packets from the legacy host driver (4 bytes per
    // packet, CIN-encoded). The Bridge decodes the CIN, feeds the MIDI
    // bytes to the slot's ByteStreamConverter, and forwards the
    // resulting MT 0x2 UMPs to the PC in the slot's first group.
    void feedHostMidi1Bytes(uint8_t idx, const uint8_t* bytes, size_t count);

    // ----- PC-side mount/alt mirror. Mirrors what m2 Device exposes
    // through Device::isMounted(); kept on the Bridge so the same
    // platform glue layer can update both sides without reaching
    // through the inner Device.
    void setDeviceMounted(bool mounted);
    void setDeviceAltSetting(uint8_t alt);

private:
    void* _state;  // pimpl
};

// Ergonomic alias to match m2device / m2host / m2ci.
using m2bridge = Bridge;

}  // namespace midi2

#pragma once

#include "midi2.h"
#include "midi2_device.h"   // re-uses ChordDescriptor + scaling helpers
#include <cstdint>
#include <functional>

#ifndef MIDI2CPP_HOST_MAX_DEVICES
#define MIDI2CPP_HOST_MAX_DEVICES 4
#endif

// Depth of the inbound RX ring (UMP packets buffered between feedRx and task).
// Size it for the worst burst the host must absorb without loss; override for
// RAM-constrained hosts. ~18 bytes per slot, usable capacity is N-1.
#ifndef MIDI2CPP_HOST_RX_RING
#define MIDI2CPP_HOST_RX_RING 256
#endif

namespace midi2 {

// ============================================================================
// Host, USB MIDI 2.0 host shape.
//
// Wraps midi2 C99 for the host-side of the protocol: an embedded MCU with a
// USB host port enumerates one or more attached MIDI 2.0 devices, queries
// their identity via UMP Stream Discovery + MIDI-CI, and routes UMPs to and
// from each. Multi-device by design: every API that touches a connected
// device takes an `idx` (0..MIDI2CPP_HOST_MAX_DEVICES-1) as its first
// argument.
//
// Platform contract is the same shape as Device, with idx-prefixed hooks:
//
//     Host::setWriteFn(WriteFn)               outbound UMP per device
//     Host::feedRx(idx, words, count)         inbound UMP from a device
//     Host::setNowFn(NowFn)                    monotonic ms clock
//     Host::notifyDeviceMounted(idx, ...)     caller wires from
//                                              tuh_midi2_mount_cb
//     Host::notifyDeviceUnmounted(idx)        caller wires from
//                                              tuh_midi2_umount_cb
//     Host::setRngFn(RngFn)                    MUID entropy (host has its
//                                              own MUID as CI Initiator)
//
// Unset hooks degrade silently. The library never includes a USB stack
// header, never calls a platform symbol it did not receive.
//
// CI Initiator state:
//
//   midi2 C99's midi2_ci_state is responder-only. Host (CI Initiator role)
//   tracks pending Discovery Inquiries and remote MUIDs in the per-device
//   DeviceIdentity. sendDiscoveryInquiry sets the pending flag and
//   request id; the inbound Discovery Reply callback validates the
//   request id, clears the flag, and populates ciMuid + ciDiscovered.
//   v0.1 ships Discovery Initiator only. Profile / PE / PI Initiator
//   flows land in v0.2 alongside m2bridge.
//
// Threading model (stream core):
//
//   feedRx only ENQUEUES into an internal single-producer/single-consumer
//   ring (O(1), no decode), so it is safe to call straight from the platform
//   RX path, including a TinyUSB rx_cb. The heavy decode + dispatch happens
//   in task(), which drains the ring on the main loop. This keeps the RX path
//   short so a burst does not starve the USB service and overflow its FIFO.
//
//   Contract: call feedRx from the RX path (one producer) and task() from the
//   main loop (one consumer). Dispatch callbacks fire from task(), not from
//   feedRx. Concurrent feedRx calls on the same Host, or calling task() from a
//   second thread, are not safe. Size the ring (MIDI2CPP_HOST_RX_RING) for the
//   worst burst; rxDropped() reports packets refused on a full ring (0 = clean).
//
// MIDI 1.0 (alt setting 0) fallback:
//
//   feedRx accepts UMP only. When a connected device exposes alt 0
//   (legacy USB-MIDI 1.0), the caller's platform layer must convert the
//   4-byte cable events to UMP MT 0x2 before calling feedRx. The
//   altSettingActive field of DeviceIdentity tracks which alt is in use
//   so the app can decide whether to expect MIDI 2.0 features (alt 1).
// ============================================================================

class Host {
public:
    static constexpr uint8_t MAX_DEVICES = MIDI2CPP_HOST_MAX_DEVICES;

    Host();
    ~Host();

    Host(const Host&)            = delete;
    Host& operator=(const Host&) = delete;

    // Lifecycle
    void begin();
    void task();

    // ------------------------------------------------------------------
    // Platform contract, caller wires these to its USB host stack.
    // ------------------------------------------------------------------
    using WriteFn = std::function<void(uint8_t idx,
                                        const uint32_t* words,
                                        size_t count)>;
    void setWriteFn(WriteFn fn);

    // Inbound UMP from a connected device. Caller drains the platform USB
    // host RX queue (e.g., tuh_midi2_ump_read on TinyUSB) and feeds the
    // words here. This only ENQUEUES into the RX ring (O(1), no decode), so
    // it is safe to call straight from the RX path; dispatch happens in
    // task(). See the threading model in the header preamble.
    //
    // MIDI 1.0 fallback: if the device exposes alt 0 (legacy USB-MIDI 1.0),
    // the platform layer must convert 4-byte cable events to UMP MT 0x2
    // before calling. The library only handles UMP (32-bit words).
    void feedRx(uint8_t idx, const uint32_t* words, size_t count);

    // Packets refused because the RX ring was full, cumulative since begin().
    // A clean run keeps this at 0; a non-zero value means the burst exceeded
    // MIDI2CPP_HOST_RX_RING or task() was not drained often enough.
    uint32_t rxDropped() const;

    using NowFn = std::function<uint32_t()>;
    void setNowFn(NowFn fn);

    using RngFn = std::function<uint32_t()>;
    void setRngFn(RngFn fn);

    // Caller invokes these from its TinyUSB / native USB host callbacks
    // (must be task context, not ISR, see header preamble).
    //
    // notifyDeviceMounted populates DeviceIdentity with the descriptor /
    // mount-callback values and fires onDeviceConnected. If
    // setAutoDiscover(true) (default) is set, an UMP Stream Endpoint
    // Discovery + a MIDI-CI Discovery Inquiry are scheduled to fire
    // shortly after the mount, populating the rest of the identity as
    // replies arrive.
    void notifyDeviceMounted(uint8_t idx,
                              uint8_t protocolVersion,
                              uint8_t cableCount,
                              uint8_t altSettingActive = 1,
                              uint16_t bcdMSC = 0x0200);
    void notifyDeviceUnmounted(uint8_t idx);

    // ------------------------------------------------------------------
    // Identity tracking, per connected device.
    //
    // Populated as we observe UMP Stream Endpoint Discovery responses and
    // MIDI-CI Discovery Replies from the device. The caller can read at
    // any time; fields are zero / empty until the device responds.
    // ------------------------------------------------------------------
    struct DeviceIdentity {
        bool     mounted;
        uint8_t  protocolVersion;          // from notifyDeviceMounted
        uint8_t  cableCount;               // from notifyDeviceMounted
        uint8_t  altSettingActive;         // 0 = MIDI 1.0 USB, 1 = MIDI 2.0 UMP
        uint16_t bcdMSC;                   // USB MIDI Streaming version (BCD)

        // Populated by UMP Stream Endpoint Info notification (status 0x01)
        uint8_t  umpVerMajor;
        uint8_t  umpVerMinor;
        bool     supportsMidi1Protocol;
        bool     supportsMidi2Protocol;
        uint8_t  numFunctionBlocks;

        // Populated by UMP Stream Device Identity notification (status 0x02)
        uint8_t  manufacturerId[3];
        uint16_t familyId;
        uint16_t modelId;
        uint32_t version;

        // Populated by UMP Stream Endpoint Name notification (status 0x03)
        char     endpointName[64];

        // Populated by UMP Stream Product Instance ID notification (status 0x04)
        char     productInstanceId[64];

        // CI Initiator state, host initiates Discovery, device replies.
        // Pending tracking lets us match the inquiry's request id to the
        // reply, time out abandoned inquiries, and keep multiple devices
        // independent.
        uint32_t ciMuid;                   // remote MUID (28-bit), populated on reply
        bool     ciDiscovered;             // true after a valid Discovery Reply
        bool     ciDiscoveryPending;       // true between sendDiscoveryInquiry and reply/timeout
        uint32_t ciDiscoveryRequestId;     // matches the inquiry's request id (0 when none in flight)
        uint32_t ciDiscoverySentMs;        // monotonic ms when the inquiry was sent (for timeout)
    };

    uint8_t                deviceCount() const;
    bool                   isDeviceMounted(uint8_t idx) const;
    const DeviceIdentity&  identity(uint8_t idx) const;

    // Host's own MUID (as CI Initiator). Auto-generated on begin() via the
    // RngFn; regenerable on collision.
    uint32_t hostMuid() const;
    void     regenerateHostMuid();

    // ------------------------------------------------------------------
    // Lifecycle callbacks (user-facing), fire when the library determines
    // a device transitioned through a meaningful state.
    // ------------------------------------------------------------------
    using DeviceConnectedCb    = std::function<void(uint8_t idx,
                                                     const DeviceIdentity&)>;
    using DeviceDisconnectedCb = std::function<void(uint8_t idx)>;
    using IdentityUpdatedCb    = std::function<void(uint8_t idx,
                                                     const DeviceIdentity&)>;

    void onDeviceConnected(DeviceConnectedCb cb);
    void onDeviceDisconnected(DeviceDisconnectedCb cb);
    void onIdentityUpdated(IdentityUpdatedCb cb);

    // ------------------------------------------------------------------
    // Inbound dispatch callbacks.
    //
    // Same UMP categories as Device, with idx prefix. For each event we
    // expose two overloads: a verbose form mirroring the wire (group,
    // attribute fields, etc) and a simple form matching MIDI 1.0 Arduino
    // libraries (channel/note/value only). Setting one form replaces a
    // previously-set form for the same event; "the latest setter wins".
    // ------------------------------------------------------------------

    // MT 0x4 MIDI 2.0 Channel Voice
    using NoteCb         = std::function<void(uint8_t idx, uint8_t group,
                                              uint8_t channel, uint8_t note,
                                              uint16_t velocity,
                                              uint8_t attrType,
                                              uint16_t attrData)>;
    using NoteSimpleCb   = std::function<void(uint8_t idx, uint8_t channel,
                                              uint8_t note, uint16_t velocity)>;

    void onNoteOn(NoteCb cb);
    void onNoteOn(NoteSimpleCb cb);
    void onNoteOff(NoteCb cb);
    void onNoteOff(NoteSimpleCb cb);

    using ControllerCb       = std::function<void(uint8_t idx, uint8_t group,
                                                  uint8_t channel,
                                                  uint8_t index, uint32_t value)>;
    using ControllerSimpleCb = std::function<void(uint8_t idx, uint8_t channel,
                                                  uint8_t index, uint32_t value)>;

    void onCC(ControllerCb cb);
    void onCC(ControllerSimpleCb cb);

    using PitchBendCb       = std::function<void(uint8_t idx, uint8_t group,
                                                 uint8_t channel,
                                                 uint32_t value)>;
    using PitchBendSimpleCb = std::function<void(uint8_t idx, uint8_t channel,
                                                 uint32_t value)>;

    void onPitchBend(PitchBendCb cb);
    void onPitchBend(PitchBendSimpleCb cb);

    using PressureCb        = std::function<void(uint8_t idx, uint8_t group,
                                                 uint8_t channel,
                                                 uint32_t value)>;
    using PressureSimpleCb  = std::function<void(uint8_t idx, uint8_t channel,
                                                 uint32_t value)>;
    void onChannelPressure(PressureCb cb);
    void onChannelPressure(PressureSimpleCb cb);

    using PolyPressureCb       = std::function<void(uint8_t idx, uint8_t group,
                                                    uint8_t channel,
                                                    uint8_t note,
                                                    uint32_t value)>;
    using PolyPressureSimpleCb = std::function<void(uint8_t idx, uint8_t channel,
                                                    uint8_t note,
                                                    uint32_t value)>;
    void onPolyPressure(PolyPressureCb cb);
    void onPolyPressure(PolyPressureSimpleCb cb);

    using PerNotePbCb     = std::function<void(uint8_t idx, uint8_t group,
                                               uint8_t channel, uint8_t note,
                                               uint32_t value)>;
    void onPerNotePitchBend(PerNotePbCb cb);

    using PerNoteCtrlCb   = std::function<void(uint8_t idx, uint8_t group,
                                               uint8_t channel, uint8_t note,
                                               uint8_t index, uint32_t value)>;
    void onRegPerNoteController(PerNoteCtrlCb cb);
    void onAsnPerNoteController(PerNoteCtrlCb cb);

    using ProgramCb       = std::function<void(uint8_t idx, uint8_t group,
                                               uint8_t channel, uint8_t program,
                                               uint8_t bankMSB, uint8_t bankLSB,
                                               bool bankValid)>;
    void onProgram(ProgramCb cb);

    // MT 0x3 SysEx7 / MT 0x5 SysEx8
    using SysEx7Cb = std::function<void(uint8_t idx, uint8_t group,
                                        const uint8_t* data, uint16_t len)>;
    void onSysEx7(SysEx7Cb cb);

    using SysEx8Cb = std::function<void(uint8_t idx, uint8_t group,
                                        uint8_t streamId,
                                        const uint8_t* data, uint16_t len)>;
    void onSysEx8(SysEx8Cb cb);

    // MT 0xD Flex Data
    using TempoCb     = std::function<void(uint8_t idx, uint8_t group,
                                           uint32_t tenNsPerQn)>;
    using TimeSigCb   = std::function<void(uint8_t idx, uint8_t group,
                                           uint8_t numerator,
                                           uint8_t denominator)>;
    using KeySigCb    = std::function<void(uint8_t idx, uint8_t group,
                                           int8_t sharpsFlats, bool minor)>;
    using ChordCb     = std::function<void(uint8_t idx, uint8_t group,
                                           const ChordDescriptor& chord)>;
    void onTempo(TempoCb cb);
    void onTimeSignature(TimeSigCb cb);
    void onKeySignature(KeySigCb cb);
    void onChord(ChordCb cb);

    // MT 0x0 Utility, JR Timestamp arrives from devices.
    using JrTimestampCb = std::function<void(uint8_t idx, uint8_t group,
                                             uint16_t timestamp)>;
    void onJRTimestamp(JrTimestampCb cb);

    // ------------------------------------------------------------------
    // Per-device senders. Same shape as Device but with idx prefix.
    // ------------------------------------------------------------------
    bool sendNoteOn(uint8_t idx, uint8_t group, uint8_t channel,
                    uint8_t note, uint16_t velocity,
                    uint8_t attrType = 0, uint16_t attrData = 0);
    bool sendNoteOff(uint8_t idx, uint8_t group, uint8_t channel,
                     uint8_t note, uint16_t velocity,
                     uint8_t attrType = 0, uint16_t attrData = 0);
    bool sendCC(uint8_t idx, uint8_t group, uint8_t channel,
                uint8_t index, uint32_t value);
    bool sendPitchBend(uint8_t idx, uint8_t group, uint8_t channel,
                       uint32_t value);
    bool sendChannelPressure(uint8_t idx, uint8_t group, uint8_t channel,
                              uint32_t value);
    bool sendPolyPressure(uint8_t idx, uint8_t group, uint8_t channel,
                           uint8_t note, uint32_t value);

    // Convenience (group=0):
    bool noteOn(uint8_t idx, uint8_t channel, uint8_t note, uint16_t velocity);
    bool noteOff(uint8_t idx, uint8_t channel, uint8_t note, uint16_t velocity = 0);
    bool cc(uint8_t idx, uint8_t channel, uint8_t index, uint32_t value);
    bool pitchBend(uint8_t idx, uint8_t channel, uint32_t value);

    // ------------------------------------------------------------------
    // CI Initiator, host queries connected devices.
    //
    // v0.1 ships Discovery only; Profile / PE / PI initiator flows land
    // in v0.2 alongside m2bridge.
    // ------------------------------------------------------------------
    bool sendDiscoveryInquiry(uint8_t idx);

    // Auto-discover newly mounted devices? When enabled (default true),
    // notifyDeviceMounted will trigger sendDiscoveryInquiry + UMP Stream
    // Endpoint Discovery automatically. Disable for manual control.
    void setAutoDiscover(bool enabled);

    // ------------------------------------------------------------------
    // Group remap, host can remap the group field on inbound UMPs from
    // a specific device, useful when forwarding to a Multi-Group Endpoint
    // downstream (the bridge use case).
    // ------------------------------------------------------------------
    void setInboundGroupRemap(uint8_t idx, const uint8_t map[16]);
    void clearInboundGroupRemap(uint8_t idx);

private:
    void* _state;   // opaque pimpl: HostState
};

// Ergonomic alias.
using m2host = Host;

}  // namespace midi2

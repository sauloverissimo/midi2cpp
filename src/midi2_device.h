#pragma once

#include "midi2.h"
#include <cstdint>
#include <functional>

#ifndef MIDI2CPP_MAX_PROFILES
#define MIDI2CPP_MAX_PROFILES 8
#endif

#ifndef MIDI2CPP_MAX_PROPERTIES
#define MIDI2CPP_MAX_PROPERTIES 8
#endif

#ifndef MIDI2CPP_MAX_SUBSCRIBERS
#define MIDI2CPP_MAX_SUBSCRIBERS 4
#endif

namespace midi2 {

class CI;  // forward declaration

struct ChordDescriptor {
  uint8_t address;
  uint8_t channel;
  int8_t  tonicSharpFlat;
  uint8_t tonicNote;
  uint8_t chordType;
  uint8_t alt1Type, alt1Degree;
  uint8_t alt2Type, alt2Degree;
  uint8_t alt3Type, alt3Degree;
  uint8_t alt4Type, alt4Degree;
  int8_t  bassSharpFlat;
  uint8_t bassNote;
  uint8_t bassChordType;
  uint8_t bassAlt1Type, bassAlt1Degree;
  uint8_t bassAlt2Type, bassAlt2Degree;
};

class Device {
  friend class CI;

public:
  Device();
  ~Device();

  void begin();
  void task();

  bool isMounted() const;
  uint8_t altSetting() const;

  // ==================== MT 0x0 Utility senders ====================
  bool sendNoop(uint8_t group);
  bool sendJRClock(uint8_t group, uint16_t timestamp);
  bool sendJRTimestamp(uint8_t group, uint16_t timestamp);
  bool sendDctpq(uint16_t tpq);
  bool sendDeltaClockstamp(uint32_t ticks);

  void enableJRHeartbeat(uint32_t intervalMs = 500);

  // ==================== MT 0x1 System senders ====================
  bool sendTuneRequest(uint8_t group);
  bool sendClock(uint8_t group);
  bool sendStart(uint8_t group);
  bool sendContinue(uint8_t group);
  bool sendStop(uint8_t group);
  bool sendActiveSensing(uint8_t group);
  bool sendSystemReset(uint8_t group);
  bool sendMTC(uint8_t group, uint8_t timeCode);
  bool sendSongSelect(uint8_t group, uint8_t songNumber);
  bool sendSongPosition(uint8_t group, uint16_t beats14);
  bool sendSystemGeneric(uint8_t group, uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0);

  // ==================== MT 0x2 MIDI 1.0 Channel Voice senders ====================
  bool sendNoteOn1(uint8_t group, uint8_t channel, uint8_t note, uint8_t velocity);
  bool sendNoteOff1(uint8_t group, uint8_t channel, uint8_t note, uint8_t velocity);
  bool sendCC1(uint8_t group, uint8_t channel, uint8_t index, uint8_t value);
  bool sendProgram1(uint8_t group, uint8_t channel, uint8_t program);
  bool sendPitchBend1(uint8_t group, uint8_t channel, uint16_t value14);
  bool sendChannelPressure1(uint8_t group, uint8_t channel, uint8_t pressure);
  bool sendPolyPressure1(uint8_t group, uint8_t channel, uint8_t note, uint8_t pressure);

  // ==================== MT 0x3 SysEx7 send ====================
  bool sendSysEx7(uint8_t group, const uint8_t* data, uint16_t len);

  // ==================== MT 0x4 MIDI 2.0 Channel Voice senders ====================
  bool sendNoteOn(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity,
                  uint8_t attrType = 0, uint16_t attrData = 0);
  bool sendNoteOff(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity,
                   uint8_t attrType = 0, uint16_t attrData = 0);
  bool sendPolyPressure(uint8_t group, uint8_t channel, uint8_t note, uint32_t pressure);
  bool sendCC(uint8_t group, uint8_t channel, uint8_t index, uint32_t value);
  bool sendRpn(uint8_t group, uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t value);
  bool sendNrpn(uint8_t group, uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t value);
  bool sendRelRpn(uint8_t group, uint8_t channel, uint8_t msb, uint8_t lsb, int32_t delta);
  bool sendRelNrpn(uint8_t group, uint8_t channel, uint8_t msb, uint8_t lsb, int32_t delta);
  bool sendProgram(uint8_t group, uint8_t channel, uint8_t program,
                   uint8_t bankMSB = 0, uint8_t bankLSB = 0, bool bankValid = false);
  bool sendChannelPressure(uint8_t group, uint8_t channel, uint32_t pressure);
  bool sendPitchBend(uint8_t group, uint8_t channel, uint32_t value);

  bool sendPerNotePitchBend(uint8_t group, uint8_t channel, uint8_t note, uint32_t value);
  bool sendRegPerNoteController(uint8_t group, uint8_t channel, uint8_t note, uint8_t index, uint32_t value);
  bool sendAsnPerNoteController(uint8_t group, uint8_t channel, uint8_t note, uint8_t index, uint32_t value);
  bool sendPerNoteManagement(uint8_t group, uint8_t channel, uint8_t note, bool detach, bool reset);

  // ==================== Convenience senders (Arduino-style) ====================
  // Group defaults to 0; MIDI 2.0 attribute fields default to 0. The most
  // common sketch case is "single group, no attribute". For Multi-Group
  // Endpoints, attribute types, or finer control, use the verbose
  // sendXxx variants above.
  bool noteOn   (uint8_t channel, uint8_t note, uint16_t velocity);
  bool noteOff  (uint8_t channel, uint8_t note, uint16_t velocity = 0);
  bool cc       (uint8_t channel, uint8_t index, uint32_t value);
  bool pitchBend(uint8_t channel, uint32_t value);

  // ==================== MT 0x5 SysEx8 send ====================
  bool sendSysEx8(uint8_t group, uint8_t streamId, const uint8_t* data, uint16_t len);
  /** Send a single-chunk Mixed Data Set: one MDS Header UMP followed by
   *  payload UMPs of up to 14 bytes each. Uses the caller's SysEx
   *  manufacturer id (16-bit form) per M2-104 7.10. */
  bool sendMds(uint8_t group, uint8_t mdsId, const uint8_t* data, uint16_t len,
               uint16_t mfrId, uint16_t deviceId = 0xFFFF,
               uint16_t subId1 = 0, uint16_t subId2 = 0);

  // ==================== MT 0xD Flex Data senders ====================
  bool sendTempo(uint8_t group, uint32_t tenNsPerQuarter);
  // num32ndNotes: SMF1 compat, 8 (= 8 thirty-seconds per quarter note) is the
  // standard default; pass a different value only when matching a non-standard
  // tempo subdivision.
  bool sendTimeSignature(uint8_t group, uint8_t numerator, uint8_t denominator,
                         uint8_t num32ndNotes = 8);
  bool sendMetronome(uint8_t group, uint8_t primary, uint8_t acc1, uint8_t acc2, uint8_t acc3,
                     uint8_t sub1, uint8_t sub2);
  bool sendKeySignature(uint8_t group, int8_t sharpsFlats, bool minor);
  bool sendChordName(uint8_t group, const ChordDescriptor& chord);
  bool sendFlexText(uint8_t group, uint8_t statusBank, uint8_t status, const char* text);

  // ==================== MT 0xF UMP Stream senders ====================
  // Endpoint Info Notification (status 0x01). Replies to host-side
  // Endpoint Discovery with the device's UMP version + protocol caps.
  bool sendEndpointInfo(uint8_t umpVerMajor, uint8_t umpVerMinor,
                        bool staticFb, uint8_t numFb,
                        bool midi2Proto, bool midi1Proto,
                        bool rxJr, bool txJr);
  bool sendDeviceIdentity(const uint8_t mfrId[3], uint16_t family, uint16_t model, uint32_t version);
  bool sendEndpointNameUpdate(const char* name);
  bool sendProductInstanceIdUpdate(const char* id);
  // Stream Configuration Notification (status 0x06). Confirms the protocol
  // currently active on the endpoint after a host Stream Config Request.
  bool sendStreamConfigNotify(uint8_t protocol);
  // Function Block Info Notification (status 0x11). Replies to host-side
  // Function Block Discovery with topology + protocol of the FB.
  // direction: 0x01=Receiver, 0x02=Sender, 0x03=Bidirectional (M2-104 §7.1.3).
  // uiHint:    0x00=Undeclared, 0x01=Receiver, 0x02=Sender, 0x03=Sender+Receiver.
  bool sendFbInfo(bool active, uint8_t fbNum,
                  uint8_t direction, uint8_t uiHint,
                  uint8_t firstGroup, uint8_t numGroups,
                  uint8_t midiCiVer, bool sysex8, uint8_t protocol);
  bool sendFbNameUpdate(uint8_t fbIdx, const char* name);
  // UMP Stream messages (MT 0xF) are endpoint-wide, not group-scoped (M2-104 §7.1).
  bool sendStartOfClip();
  bool sendEndOfClip();

  // ==================== Callbacks, MT 0x0 Utility ====================
  using UtilityCb = std::function<void(uint8_t group, uint16_t value)>;
  using NoArgCb   = std::function<void(uint8_t group)>;

  void onNoop(NoArgCb cb);
  void onJRClock(UtilityCb cb);
  void onJRTimestamp(UtilityCb cb);
  void onDctpq(std::function<void(uint16_t tpq)> cb);
  void onDeltaClockstamp(std::function<void(uint32_t ticks)> cb);

  // ==================== Callbacks, MT 0x1 System ====================
  using SystemCb = std::function<void(uint8_t group, uint8_t status, uint8_t data1, uint8_t data2)>;
  void onSystem(SystemCb cb);

  // ==================== Callbacks, MT 0x2 MIDI 1.0 ====================
  using Note1Cb         = std::function<void(uint8_t g, uint8_t ch, uint8_t note, uint8_t vel)>;
  using Controller1Cb   = std::function<void(uint8_t g, uint8_t ch, uint8_t idx, uint8_t val)>;
  using Program1Cb      = std::function<void(uint8_t g, uint8_t ch, uint8_t prog)>;
  using PitchBend1Cb    = std::function<void(uint8_t g, uint8_t ch, uint16_t val14)>;
  using Pressure1Cb     = std::function<void(uint8_t g, uint8_t ch, uint8_t pressure)>;
  using PolyPressure1Cb = std::function<void(uint8_t g, uint8_t ch, uint8_t note, uint8_t pressure)>;

  void onNoteOn1(Note1Cb cb);
  void onNoteOff1(Note1Cb cb);
  void onCC1(Controller1Cb cb);
  void onProgram1(Program1Cb cb);
  void onPitchBend1(PitchBend1Cb cb);
  void onChannelPressure1(Pressure1Cb cb);
  void onPolyPressure1(PolyPressure1Cb cb);

  void setUpscaleMt2(bool enabled);

  // ==================== Callbacks, MT 0x3 SysEx7 ====================
  using SysEx7Cb = std::function<void(uint8_t group, const uint8_t* data, uint16_t len)>;
  void onSysEx7(SysEx7Cb cb);

  // ==================== Callbacks, MT 0x4 MIDI 2.0 ====================
  using NoteCb           = std::function<void(uint8_t g, uint8_t ch, uint8_t note, uint16_t vel,
                                              uint8_t attrType, uint16_t attrData)>;
  using ControllerCb32   = std::function<void(uint8_t g, uint8_t ch, uint8_t idx, uint32_t val)>;
  using Pb32Cb           = std::function<void(uint8_t g, uint8_t ch, uint32_t val)>;
  using ProgramCb        = std::function<void(uint8_t g, uint8_t ch, uint8_t prog,
                                              uint8_t bankMSB, uint8_t bankLSB, bool bankValid)>;
  using PressureCb32     = std::function<void(uint8_t g, uint8_t ch, uint32_t val)>;
  using PolyPressureCb32 = std::function<void(uint8_t g, uint8_t ch, uint8_t note, uint32_t val)>;
  using PerNotePbCb      = std::function<void(uint8_t g, uint8_t ch, uint8_t note, uint32_t val)>;
  using PerNoteCtrlCb    = std::function<void(uint8_t g, uint8_t ch, uint8_t note, uint8_t idx, uint32_t val)>;
  using PerNoteMgmtCb    = std::function<void(uint8_t g, uint8_t ch, uint8_t note, bool detach, bool reset)>;
  using RpnNrpnCb        = std::function<void(uint8_t g, uint8_t ch, uint8_t msb, uint8_t lsb, uint32_t val)>;
  using RelRpnNrpnCb     = std::function<void(uint8_t g, uint8_t ch, uint8_t msb, uint8_t lsb, int32_t delta)>;

  // Arduino-style simple callback signatures: drop `group` and the
  // attribute fields. Set via overloads of onNoteOn / onNoteOff / onCC /
  // onPitchBend below. The verbose form keeps full spec fidelity; the
  // simple form is for sketches that only care about (channel, note, value).
  using NoteSimpleCb       = std::function<void(uint8_t ch, uint8_t note, uint16_t vel)>;
  using ControllerSimpleCb = std::function<void(uint8_t ch, uint8_t idx, uint32_t val)>;
  using PbSimpleCb         = std::function<void(uint8_t ch, uint32_t val)>;

  void onNoteOn(NoteCb cb);
  void onNoteOn(NoteSimpleCb cb);            // Arduino-style overload
  void onNoteOff(NoteCb cb);
  void onNoteOff(NoteSimpleCb cb);           // Arduino-style overload
  void onPolyPressure(PolyPressureCb32 cb);
  void onCC(ControllerCb32 cb);
  void onCC(ControllerSimpleCb cb);          // Arduino-style overload
  void onProgram(ProgramCb cb);
  void onChannelPressure(PressureCb32 cb);
  void onPitchBend(Pb32Cb cb);
  void onPitchBend(PbSimpleCb cb);           // Arduino-style overload
  void onRpn(RpnNrpnCb cb);
  void onNrpn(RpnNrpnCb cb);
  void onRelRpn(RelRpnNrpnCb cb);
  void onRelNrpn(RelRpnNrpnCb cb);
  void onPerNotePitchBend(PerNotePbCb cb);
  void onRegPerNoteController(PerNoteCtrlCb cb);
  void onAsnPerNoteController(PerNoteCtrlCb cb);
  void onPerNoteManagement(PerNoteMgmtCb cb);

  // ==================== Callbacks, MT 0x5 SysEx8 ====================
  using SysEx8Cb = std::function<void(uint8_t group, uint8_t streamId, const uint8_t* data, uint16_t len)>;
  void onSysEx8(SysEx8Cb cb);

  // ==================== Callbacks, MT 0xD Flex Data ====================
  using TempoCb     = std::function<void(uint8_t group, uint32_t tenNsPerQn)>;
  using TimeSigCb   = std::function<void(uint8_t group, uint8_t num, uint8_t denom)>;
  using MetronomeCb = std::function<void(uint8_t group, uint8_t primary, uint8_t acc1, uint8_t acc2,
                                         uint8_t acc3, uint8_t sub1, uint8_t sub2)>;
  using KeySigCb    = std::function<void(uint8_t group, int8_t sharpsFlats, bool minor)>;
  using ChordCb     = std::function<void(uint8_t group, const ChordDescriptor& chord)>;
  using FlexTextCb  = std::function<void(uint8_t group, uint8_t statusBank, uint8_t status,
                                         const char* text, uint16_t len)>;

  void onTempo(TempoCb cb);
  void onTimeSignature(TimeSigCb cb);
  void onMetronome(MetronomeCb cb);
  void onKeySignature(KeySigCb cb);
  void onChord(ChordCb cb);
  void onFlexText(FlexTextCb cb);

  // ==================== Callbacks, MT 0xF UMP Stream ====================
  using EndpointDiscoveryCb = std::function<void(uint8_t filter)>;
  using EndpointInfoCb      = std::function<void(uint8_t umpMaj, uint8_t umpMin,
                                                 bool staticFb, uint8_t numFb,
                                                 bool midi2, bool midi1, bool rxJr, bool txJr)>;
  using DeviceIdentityCb    = std::function<void(const uint8_t mfrId[3], uint16_t family,
                                                 uint16_t model, uint32_t version)>;
  using StreamTextCb        = std::function<void(uint16_t status, uint8_t format,
                                                 const char* text, uint16_t len)>;
  using FbNameCb            = std::function<void(uint8_t fbIdx, uint8_t format,
                                                 const char* text, uint16_t len)>;
  using StreamConfigCb      = std::function<void(uint8_t protocol)>;
  using FbDiscoveryCb       = std::function<void(uint8_t fbNum, uint8_t filter)>;
  using FbInfoCb            = std::function<void(bool active, uint8_t fbNum,
                                                 uint8_t direction, uint8_t uiHint,
                                                 uint8_t firstGroup, uint8_t numGroups,
                                                 uint8_t ciVersion, bool sysex8, uint8_t protocol)>;
  using ClipCb              = std::function<void(uint8_t group, uint16_t status)>;

  void onEndpointDiscovery(EndpointDiscoveryCb cb);
  void onEndpointInfo(EndpointInfoCb cb);
  void onDeviceIdentity(DeviceIdentityCb cb);
  void onStreamText(StreamTextCb cb);
  void onFbName(FbNameCb cb);
  void onStreamConfigRequest(StreamConfigCb cb);
  void onStreamConfigNotify(StreamConfigCb cb);
  void onFbDiscovery(FbDiscoveryCb cb);
  void onFbInfo(FbInfoCb cb);
  void onClip(ClipCb cb);

  // ==================== Group remap ====================
  void setGroupRemap(const uint8_t map[16]);

  // ==================== Bit scaling (static, M2-115) ====================
  static uint16_t scaleUp7to16(uint8_t v7)    { return midi2_msg_scale_up_7to16(v7); }
  static uint32_t scaleUp7to32(uint8_t v7)    { return midi2_msg_scale_up_7to32(v7); }
  static uint32_t scaleUp14to32(uint16_t v14) { return midi2_msg_scale_up_14to32(v14); }
  static uint8_t  scaleDown16to7(uint16_t v)  { return midi2_msg_scale_down_16to7(v); }
  static uint8_t  scaleDown32to7(uint32_t v)  { return midi2_msg_scale_down_32to7(v); }
  static uint16_t scaleDown32to14(uint32_t v) { return midi2_msg_scale_down_32to14(v); }

  // ==================== Field-tested helpers ====================
  static bool downgradeMt4ToMt2(const uint32_t* in, uint8_t count, uint32_t* out, uint8_t* outCount);
  static bool cableEventToUmp(uint32_t cableEvent, uint8_t group, uint32_t* umpOut);
  static void setUmpGroup(uint32_t* word0, uint8_t group);

  // ==================== Platform contract (caller wires transport) ====================
  // The library is platform-agnostic: it does not include TinyUSB, Pico SDK,
  // ESP-IDF, or any USB driver. The caller (sketch / example / firmware)
  // pumps UMP words through these four hooks. See examples/ for per-platform
  // wiring recipes (Adafruit_TinyUSB_Arduino, Pico SDK + tinyusb, ESP-IDF
  // tinyusb component, Teensy native, libDaisy USBMidi, generic TinyUSB).

  // Outbound UMP. Library calls fn(words, count) for every sendXxx() and for
  // the JR Timestamp heartbeat. The caller forwards to the platform's USB
  // MIDI write API. Returning is implicit, back-pressure is reported via
  // the bool return of sendXxx() (set false from inside `fn` by tracking
  // via the captured context, or just queue and return).
  using WriteFn = std::function<void(const uint32_t* words, size_t count)>;
  void setWriteFn(WriteFn fn);

  // Inbound UMP. Caller invokes feedRx(words, count) from its USB MIDI RX
  // callback (e.g. tud_midi_rx_cb on TinyUSB). Library parses, reassembles
  // SysEx, and dispatches to the registered onXxx callbacks.
  void feedRx(const uint32_t* words, size_t count);

  // Optional clock source for the JR Timestamp heartbeat. Caller supplies a
  // monotonic ms function (millis() on Arduino, time_us_64()/1000 on Pico
  // SDK, esp_timer_get_time()/1000 on ESP-IDF). If unset, the heartbeat
  // never fires.
  using NowFn = std::function<uint32_t()>;
  void setNowFn(NowFn fn);

  // USB lifecycle. Caller informs the library when the device has enumerated
  // and which alt setting is active (1 = MIDI 2.0 stream, 0 = MIDI 1.0
  // fallback). Library uses this to gate sendXxx and to know when to start
  // the heartbeat.
  void setMounted(bool mounted);
  void setAltSetting(uint8_t alt);

  // Direct access to the internal midi2_proc_state. Used by power-user code
  // that wants to feed words directly through proc (for example to test
  // SysEx reassembly with synthesized fragments). Most callers should use
  // feedRx() instead.
  midi2_proc_state* procState();

private:
  // Friend-access entry points for class CI. Not part of the public API.
  void _setCiSysExHook(SysEx7Cb hook);
  using CiWriteFn = uint32_t (*)(const uint32_t*, uint32_t, void*);
  void _ciWriteFnContext(CiWriteFn* outFn, void** outCtx);

  void* _state;  // opaque pimpl: midi2_proc_state + midi2_dispatch + user callbacks
};

// ByteStreamConverter: MIDI 1.0 DIN byte stream → UMP MT 0x2
class ByteStreamConverter {
public:
  ByteStreamConverter(uint8_t group = 0);
  ~ByteStreamConverter();

  bool feed(uint8_t byte);
  using UmpCb = std::function<void(const uint32_t* words, uint8_t count)>;
  void onUmp(UmpCb cb);
  void reset();
  void setGroup(uint8_t g);

private:
  void* _state;
};

}  // namespace midi2

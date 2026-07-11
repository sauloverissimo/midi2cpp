#pragma once

#include "midi2.h"
#include "midi2_device.h"
#include <cstdint>
#include <functional>

namespace midi2 {

class CI {
public:
  // ==================== Construction ====================
  CI(Device& device);
  ~CI();

  void begin(const uint8_t manufacturerId[3],
             uint16_t family,
             uint16_t model,
             uint32_t version,
             uint8_t ciCat = 0x1C);

  // ==================== MUID ====================
  uint32_t muid() const;
  void regenerateMuid();

  // ==================== Discovery ====================
  using NoArgCb = std::function<void()>;
  void onDiscovery(NoArgCb cb);
  void onDiscoveryReply(NoArgCb cb);
  bool sendDiscoveryInquiry();

  // ==================== Endpoint Info ====================
  // Reception only in v0.1; sender helpers deferred (Initiator role).
  using EndpointInfoInquiryCb = std::function<void(uint8_t status)>;
  using EndpointInfoReplyCb   = std::function<void(uint8_t status, const uint8_t* data, uint16_t len)>;

  void onEndpointInfo(EndpointInfoInquiryCb cb);
  void onEndpointInfoReply(EndpointInfoReplyCb cb);

  // ==================== ACK / NAK / Invalidate MUID ====================
  using AckNakCb = std::function<void(uint8_t origSubId, uint8_t status, const uint8_t* details)>;
  void onAck(AckNakCb cb);
  void onNak(AckNakCb cb);
  // ACK/NAK senders are emitted automatically by the convenience responder
  // (set_nak_on_unknown enabled in begin()). Manual builders deferred.

  void onInvalidateMuid(std::function<void(uint32_t targetMuid)> cb);

  // ==================== Profile Configuration ====================
  using ProfileIdCb      = std::function<void(const uint8_t id[5])>;
  using ProfileOnOffCb   = std::function<void(const uint8_t id[5], uint8_t numChannels)>;
  using ProfileDetailsCb = std::function<void(const uint8_t id[5], uint8_t inquiryTarget)>;
  using ProfileDataCb    = std::function<void(const uint8_t id[5], const uint8_t* data, uint16_t len)>;

  int addProfile(const uint8_t id[5], bool alwaysOn = false);
  int removeProfile(const uint8_t id[5]);

  void onProfileInquiry(std::function<void()> cb);
  void onProfileEnable(ProfileOnOffCb cb);
  void onProfileDisable(ProfileOnOffCb cb);
  void onProfileAdded(ProfileIdCb cb);
  void onProfileRemoved(ProfileIdCb cb);
  void onProfileDetailsInquiry(ProfileDetailsCb cb);
  // sendProfileDetailsReply / sendProfileSpecificData deferred to v0.1.x
  //, convenience responder auto-replies to inquiries from registered
  // profiles; explicit senders only matter for app-driven flows.

  void onProfileSpecificData(ProfileDataCb cb);

  // ==================== Property Exchange ====================
  using PeGetter = std::function<const char*()>;
  using PeSetter = std::function<bool(const char* value)>;

  int addProperty(const char* name, PeGetter getter, PeSetter setter = nullptr);
  int addPropertyStatic(const char* name, const char* staticValue);
  int setPropertySubscribable(const char* name, bool enabled);
  uint8_t subscriberCount() const;
  void notifyPropertyChanged(const char* name);
  int removeProperty(const char* name);

  // PE callbacks deliver raw bytes (header + optional body). M2-103 payloads
  // can exceed 256 bytes, DeviceInfo, ChannelList, etc. Caller picks the
  // parsing strategy (cJSON, picojson, hand-roll).
  using PeCapsCb      = std::function<void(uint8_t maxSimultaneous, uint8_t peMajor, uint8_t peMinor)>;
  using PeGetCb       = std::function<void(const uint8_t* header, uint16_t headerLen)>;
  using PeSetCb       = std::function<void(const uint8_t* header, uint16_t headerLen,
                                           const uint8_t* body,   uint16_t bodyLen)>;
  using PeSubscribeCb = std::function<void(const uint8_t* header, uint16_t headerLen)>;
  using PeNotifyCb    = std::function<void(const uint8_t* header, uint16_t headerLen,
                                           const uint8_t* body,   uint16_t bodyLen)>;

  void onPECapability(PeCapsCb cb);
  void onPEGet(PeGetCb cb);
  void onPESet(PeSetCb cb);
  void onPESubscribe(PeSubscribeCb cb);
  void onPENotify(PeNotifyCb cb);

  // ==================== Process Inquiry ====================
  void setMidiReport(uint8_t msgDataControl,
                     uint64_t systemBitmap,
                     uint64_t channelBitmap,
                     uint64_t noteBitmap);

  using PiCapsCb = std::function<void()>;
  void onPICapability(PiCapsCb cb);

  using PiMidiReportCb = std::function<void(uint8_t msgDataCtl)>;
  void onMidiReportInquiry(PiMidiReportCb cb);

  // ==================== Config ====================
  // setMaxSysexSize removed for v0.1: midi2 C99 v0.3.0 does not expose a
  // setter for the advertised max SysEx size in Discovery Reply, and the
  // wrapper-side stub was a documented no-op. Re-add when upstream provides
  // a setter.

  // Caller-supplied entropy source for MUID generation (regenerate, collision
  // resolution, Invalidate MUID). The platform decides what backs it:
  // esp_random on ESP-IDF, get_rand_32 on Pico SDK, random() on Arduino,
  // std::rand on host tests. If unset, MUID stays at the value seeded in
  // begin(). The library intentionally does not pull a platform header.
  using RngFn = std::function<uint32_t()>;
  void setRngFn(RngFn fn);

private:
  Device* _device;
  void* _state;
};

}  // namespace midi2

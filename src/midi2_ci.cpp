#include "midi2_ci.h"
#include <cstring>

namespace midi2 {

// ============================================================================
// CIState — internal pimpl held behind Device::_state's sibling pointer.
// Stores midi2_ci state, the granular dispatch (33 callbacks), caller-provided
// arrays sized by MIDI2CPP_MAX_*, and ~20 std::function slots for the
// high-level callbacks the wrapper exposes (Discovery, ACK/NAK, Profile,
// PE, PI). The raw 33-callback dispatch is intentionally not all surfaced —
// the wrapper picks a coarser, ergonomic subset.
// ============================================================================

struct CIState {
    midi2_ci_state    ci;
    midi2_ci_dispatch dispatch;

    // Caller-provided storage (compile-time tunables: see midi2_device.h).
    uint8_t           profile_storage[MIDI2CPP_MAX_PROFILES][5];
    midi2_ci_property property_storage[MIDI2CPP_MAX_PROPERTIES];
    midi2_ci_subscriber subscriber_storage[MIDI2CPP_MAX_SUBSCRIBERS];

    // PE app-side getter/setter handlers parallel to property_storage so
    // the C trampolines can route Get/Set into std::function lambdas.
    CI::PeGetter pe_getters[MIDI2CPP_MAX_PROPERTIES];
    CI::PeSetter pe_setters[MIDI2CPP_MAX_PROPERTIES];
    // PE Getter must return a const char* whose lifetime extends past the
    // call (midi2_ci copies it into the reply SysEx). User getters typically
    // return into stack-local buffers, so we cache here. 1 KB covers most
    // M2-103 properties (DeviceInfo, ChannelList) without dynamic alloc.
    // Replace with chunked PE for larger payloads (deferred to v0.1.x).
    char         pe_value_scratch[1024];

    // High-level wrapper callbacks (curated subset).
    CI::NoArgCb                                            cb_discovery;
    CI::NoArgCb                                            cb_discovery_reply;
    CI::EndpointInfoInquiryCb                              cb_endpoint_info;
    CI::EndpointInfoReplyCb                                cb_endpoint_info_reply;
    CI::AckNakCb                                           cb_ack;
    CI::AckNakCb                                           cb_nak;
    std::function<void(uint32_t)>                          cb_invalidate_muid;
    std::function<void()>                                  cb_profile_inquiry;
    CI::ProfileOnOffCb                                     cb_profile_enable;
    CI::ProfileOnOffCb                                     cb_profile_disable;
    CI::ProfileIdCb                                        cb_profile_added;
    CI::ProfileIdCb                                        cb_profile_removed;
    CI::ProfileDetailsCb                                   cb_profile_details_inquiry;
    CI::ProfileDataCb                                      cb_profile_specific_data;
    CI::PeCapsCb                                           cb_pe_capability;
    CI::PeGetCb                                            cb_pe_get;
    CI::PeSetCb                                            cb_pe_set;
    CI::PeSubscribeCb                                      cb_pe_subscribe;
    CI::PeNotifyCb                                         cb_pe_notify;
    CI::PiCapsCb                                           cb_pi_capability;
    CI::PiMidiReportCb                                     cb_midi_report_inquiry;

    // Process Inquiry MIDI Report bitmaps (set via setMidiReport)
    uint8_t  midi_report_data_control;
    uint64_t midi_report_system_bitmap;
    uint64_t midi_report_channel_bitmap;
    uint64_t midi_report_note_bitmap;

    // Owning Device pointer (for write_fn delegation)
    Device* device;

    // Caller-supplied entropy source. If unset, the trampoline returns 0,
    // which keeps MUID frozen but link-safe on every platform.
    CI::RngFn rng_fn;
};

static inline CIState* cst(void* p) { return static_cast<CIState*>(p); }

// ============================================================================
// Trampolines: midi2_ci_dispatch C callbacks -> CIState C++ std::function.
// All take (..., void* context) where context = CIState*.
// ============================================================================

namespace {

// Management
void tramp_ci_discovery(midi2_ci_header /*hdr*/, uint32_t /*mfr*/, uint16_t /*fam*/,
                        uint16_t /*mod*/, uint32_t /*ver*/, uint8_t /*cat*/,
                        uint32_t /*max_sx*/, uint8_t /*out_path*/, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_discovery) s->cb_discovery();
}

void tramp_ci_discovery_reply(midi2_ci_header /*hdr*/, uint32_t /*mfr*/, uint16_t /*fam*/,
                              uint16_t /*mod*/, uint32_t /*ver*/, uint8_t /*cat*/,
                              uint32_t /*max_sx*/, uint8_t /*out_path*/,
                              uint8_t /*fb*/, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_discovery_reply) s->cb_discovery_reply();
}

void tramp_ci_endpoint_info(midi2_ci_header /*hdr*/, uint8_t status, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_endpoint_info) s->cb_endpoint_info(status);
}

void tramp_ci_endpoint_info_reply(midi2_ci_header /*hdr*/, uint8_t status,
                                   const uint8_t* data, uint16_t len, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_endpoint_info_reply) s->cb_endpoint_info_reply(status, data, len);
}

void tramp_ci_invalidate_muid(midi2_ci_header /*hdr*/, uint32_t target, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_invalidate_muid) s->cb_invalidate_muid(target);
}

void tramp_ci_ack(midi2_ci_header /*hdr*/, uint8_t orig_sub_id, uint8_t status,
                  uint8_t /*status_data*/, const uint8_t* /*details*/,
                  uint16_t /*msg_len*/, const uint8_t* /*msg*/, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_ack) s->cb_ack(orig_sub_id, status, nullptr);
}

void tramp_ci_nak(midi2_ci_header /*hdr*/, uint8_t orig_sub_id, uint8_t status,
                  uint8_t /*status_data*/, const uint8_t* /*details*/,
                  uint16_t /*msg_len*/, const uint8_t* /*msg*/, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_nak) s->cb_nak(orig_sub_id, status, nullptr);
}

// Profile
void tramp_ci_profile_inquiry(midi2_ci_header /*hdr*/, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_profile_inquiry) s->cb_profile_inquiry();
}

void tramp_ci_set_profile_on(midi2_ci_header /*hdr*/, const uint8_t* pid,
                              uint16_t num_channels, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_profile_enable) s->cb_profile_enable(pid, (uint8_t)num_channels);
}

void tramp_ci_set_profile_off(midi2_ci_header /*hdr*/, const uint8_t* pid,
                               uint16_t num_channels, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_profile_disable) s->cb_profile_disable(pid, (uint8_t)num_channels);
}

void tramp_ci_profile_added(midi2_ci_header /*hdr*/, const uint8_t* pid, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_profile_added) s->cb_profile_added(pid);
}

void tramp_ci_profile_removed(midi2_ci_header /*hdr*/, const uint8_t* pid, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_profile_removed) s->cb_profile_removed(pid);
}

void tramp_ci_profile_details(midi2_ci_header /*hdr*/, const uint8_t* pid,
                               uint8_t target, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_profile_details_inquiry) s->cb_profile_details_inquiry(pid, target);
}

void tramp_ci_profile_specific(midi2_ci_header /*hdr*/, const uint8_t* pid,
                                const uint8_t* data, uint32_t len, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_profile_specific_data) s->cb_profile_specific_data(pid, data, (uint16_t)len);
}

// Property Exchange
void tramp_ci_pe_capability(midi2_ci_header /*hdr*/, uint8_t max_simul,
                             uint8_t pe_major, uint8_t pe_minor, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_pe_capability) s->cb_pe_capability(max_simul, pe_major, pe_minor);
}

// PE Get/Set/Subscribe/Notify all forward the raw header (and body where
// applicable) so the caller can parse JSON without truncation.
void tramp_ci_pe_get(midi2_ci_header /*hdr*/, uint8_t /*req*/,
                      const uint8_t* hdr_data, uint16_t hdr_len,
                      uint16_t /*nc*/, uint16_t /*tc*/,
                      const uint8_t* /*body*/, uint16_t /*body_len*/, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_pe_get) s->cb_pe_get(hdr_data, hdr_len);
}

void tramp_ci_pe_subscribe(midi2_ci_header /*hdr*/, uint8_t /*req*/,
                            const uint8_t* hdr_data, uint16_t hdr_len,
                            uint16_t /*nc*/, uint16_t /*tc*/,
                            const uint8_t* /*body*/, uint16_t /*body_len*/, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_pe_subscribe) s->cb_pe_subscribe(hdr_data, hdr_len);
}

void tramp_ci_pe_set(midi2_ci_header /*hdr*/, uint8_t /*req*/,
                      const uint8_t* hdr_data, uint16_t hdr_len,
                      uint16_t /*nc*/, uint16_t /*tc*/,
                      const uint8_t* body, uint16_t body_len, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_pe_set) s->cb_pe_set(hdr_data, hdr_len, body, body_len);
}

void tramp_ci_pe_notify(midi2_ci_header /*hdr*/, uint8_t /*req*/,
                         const uint8_t* hdr_data, uint16_t hdr_len,
                         uint16_t /*nc*/, uint16_t /*tc*/,
                         const uint8_t* body, uint16_t body_len, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_pe_notify) s->cb_pe_notify(hdr_data, hdr_len, body, body_len);
}

// Process Inquiry
void tramp_ci_pi_capability(midi2_ci_header /*hdr*/, void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_pi_capability) s->cb_pi_capability();
}

void tramp_ci_pi_midi_report(midi2_ci_header /*hdr*/, uint8_t mdc,
                              uint8_t /*sys*/, uint8_t /*ch*/, uint8_t /*note*/,
                              void* ctx) {
    auto* s = cst(ctx);
    if (s->cb_midi_report_inquiry) s->cb_midi_report_inquiry(mdc);
}

// PE getter/setter trampolines (called by midi2_ci_process_sysex when it
// receives PE Get/Set for a registered dynamic property).
const char* tramp_pe_getter(const char* name, void* context) {
    auto* s = cst(context);
    for (uint8_t i = 0; i < s->ci.property_count; ++i) {
        if (s->ci.properties[i].name && std::strcmp(s->ci.properties[i].name, name) == 0
            && s->pe_getters[i]) {
            const char* v = s->pe_getters[i]();
            // Cache so the C99 layer can reference the string after we return.
            if (v) {
                std::strncpy(s->pe_value_scratch, v, sizeof(s->pe_value_scratch) - 1);
                s->pe_value_scratch[sizeof(s->pe_value_scratch) - 1] = '\0';
                return s->pe_value_scratch;
            }
        }
    }
    return "";
}

bool tramp_pe_setter(const char* name, const char* value, void* context) {
    auto* s = cst(context);
    for (uint8_t i = 0; i < s->ci.property_count; ++i) {
        if (s->ci.properties[i].name && std::strcmp(s->ci.properties[i].name, name) == 0
            && s->pe_setters[i]) {
            return s->pe_setters[i](value);
        }
    }
    return false;
}

// RNG trampoline. Delegates to the caller-supplied RngFn (set via
// CI::setRngFn). Returns 0 if no RNG was wired — MUID stays at the value
// seeded in begin(), which keeps the library link-safe on every platform
// without pulling esp_random / get_rand_32 / Arduino random into the public
// surface.
uint32_t tramp_ci_rng(void* context) {
    auto* s = static_cast<CIState*>(context);
    return s && s->rng_fn ? s->rng_fn() : 0u;
}

}  // anonymous namespace

// ============================================================================
// CI public API
// ============================================================================

CI::CI(Device& device) : _device(&device), _state(nullptr) {
    _state = new CIState{};
    cst(_state)->device = &device;
}

CI::~CI() {
    // clear Device's CI hook before our state goes away. Without
    // this, an inbound SysEx7 between ~CI() and ~Device() would invoke the
    // captured lambda whose CIState* points to freed memory.
    if (_device) _device->_setCiSysExHook(nullptr);
    delete cst(_state);
}

void CI::begin(const uint8_t manufacturerId[3],
               uint16_t family, uint16_t model, uint32_t version,
               uint8_t ciCat) {
    auto* s = cst(_state);

    // Seed for the initial MUID; will be regenerated via tramp_ci_rng if
    // collision or Invalidate MUID arrives.
    uint32_t seed = (uint32_t)((uintptr_t)s ^ (manufacturerId ? *manufacturerId : 0));

    midi2_ci_init_ex(&s->ci, seed,
                     s->profile_storage,    MIDI2CPP_MAX_PROFILES,
                     s->property_storage,   MIDI2CPP_MAX_PROPERTIES,
                     s->subscriber_storage, MIDI2CPP_MAX_SUBSCRIBERS);

    // midi2_ci invokes property getter/setter as
    //   getter(name, state->context) / setter(name, value, state->context)
    // so context must point to our CIState for the trampolines to find the
    // user's std::function lambdas in pe_getters[]/pe_setters[].
    s->ci.context = s;

    // Pack mfrId[3] MSB-first into 24-bit.
    uint32_t mfr = manufacturerId
                 ? ((uint32_t)manufacturerId[0] << 16)
                 | ((uint32_t)manufacturerId[1] << 8)
                 |  (uint32_t)manufacturerId[2]
                 : 0;
    midi2_ci_set_identity(&s->ci, mfr, family, model, version);

    // Advertise the requested Capability Inquiry Categories in the Discovery
    // Reply (midi2 v0.6.1+). Default 0x1C = Profile Config | Property Exchange
    // | Process Inquiry. The declared MIDI-CI Message Version (0x02) is fixed
    // by the core; ciCat only selects which categories are announced.
    midi2_ci_set_capabilities(&s->ci, ciCat);

    // Bridge to Device for SysEx TX.
    Device::CiWriteFn wfn = nullptr;
    void* wctx = nullptr;
    _device->_ciWriteFnContext(&wfn, &wctx);
    midi2_ci_set_write_fn(&s->ci, wfn, wctx);

    midi2_ci_set_rng(&s->ci, &tramp_ci_rng, s);
    midi2_ci_set_nak_on_unknown(&s->ci, true);
    midi2_ci_set_auto_invalidate_on_collision(&s->ci, true);

    // Wire dispatch trampolines for the high-level callbacks.
    midi2_ci_dispatch_init(&s->dispatch);
    s->dispatch.context                  = s;
    s->dispatch.on_discovery             = tramp_ci_discovery;
    s->dispatch.on_discovery_reply       = tramp_ci_discovery_reply;
    s->dispatch.on_endpoint_info         = tramp_ci_endpoint_info;
    s->dispatch.on_endpoint_info_reply   = tramp_ci_endpoint_info_reply;
    s->dispatch.on_invalidate_muid       = tramp_ci_invalidate_muid;
    s->dispatch.on_ack                   = tramp_ci_ack;
    s->dispatch.on_nak                   = tramp_ci_nak;
    s->dispatch.on_profile_inquiry       = tramp_ci_profile_inquiry;
    s->dispatch.on_set_profile_on        = tramp_ci_set_profile_on;
    s->dispatch.on_set_profile_off       = tramp_ci_set_profile_off;
    s->dispatch.on_profile_added         = tramp_ci_profile_added;
    s->dispatch.on_profile_removed       = tramp_ci_profile_removed;
    s->dispatch.on_profile_details       = tramp_ci_profile_details;
    s->dispatch.on_profile_specific_data = tramp_ci_profile_specific;
    s->dispatch.on_pe_capability         = tramp_ci_pe_capability;
    s->dispatch.on_pe_get                = tramp_ci_pe_get;
    s->dispatch.on_pe_set                = tramp_ci_pe_set;
    s->dispatch.on_pe_subscribe          = tramp_ci_pe_subscribe;
    s->dispatch.on_pe_notify             = tramp_ci_pe_notify;
    s->dispatch.on_pi_capability         = tramp_ci_pi_capability;
    s->dispatch.on_pi_midi_report        = tramp_ci_pi_midi_report;

    // Hook reassembled SysEx7 from Device into CI: dispatch_feed (notify)
    // + process_sysex (auto-respond) on every CI message.
    _device->_setCiSysExHook([s](uint8_t group, const uint8_t* data, uint16_t len) {
        midi2_ci_dispatch_feed(&s->dispatch, group, data, len);
        midi2_ci_process_sysex(&s->ci, group, data, len);
    });
}

uint32_t CI::muid() const            { return cst(_state)->ci.muid; }
void     CI::regenerateMuid()        { (void)midi2_ci_new_muid(&cst(_state)->ci); }

void CI::setRngFn(RngFn fn)          { cst(_state)->rng_fn = std::move(fn); }

// Discovery
void CI::onDiscovery(NoArgCb cb)         { cst(_state)->cb_discovery       = std::move(cb); }
void CI::onDiscoveryReply(NoArgCb cb)    { cst(_state)->cb_discovery_reply = std::move(cb); }

bool CI::sendDiscoveryInquiry() {
    auto* s = cst(_state);
    if (!s->ci.write_fn) return false;
    uint8_t buf[64];
    // CI v2 broadcast Discovery Inquiry advertising the same identity that
    // begin() configured. ci_category 0x1C = Profile + PE + PI.
    uint16_t len = midi2_ci_build_discovery(buf,
        MIDI2_CI_VERSION_2,
        s->ci.muid,
        s->ci.manufacturer_id, s->ci.family_id, s->ci.model_id,
        s->ci.version_id,
        /*ci_category*/ 0x1C,
        /*max_sysex*/ 512,
        /*output_path*/ 0);
    midi2_proc_send_sysex7(/*group*/ 0, buf, len, s->ci.write_fn, s->ci.write_context);
    return true;
}

// Endpoint Info
void CI::onEndpointInfo(EndpointInfoInquiryCb cb)    { cst(_state)->cb_endpoint_info       = std::move(cb); }
void CI::onEndpointInfoReply(EndpointInfoReplyCb cb) { cst(_state)->cb_endpoint_info_reply = std::move(cb); }

// ACK / NAK / Invalidate MUID
void CI::onAck(AckNakCb cb)       { cst(_state)->cb_ack = std::move(cb); }
void CI::onNak(AckNakCb cb)       { cst(_state)->cb_nak = std::move(cb); }
void CI::onInvalidateMuid(std::function<void(uint32_t)> cb) {
    cst(_state)->cb_invalidate_muid = std::move(cb);
}

// Profile Configuration
int CI::addProfile(const uint8_t id[5], bool /*alwaysOn*/) {
    return midi2_ci_add_profile(&cst(_state)->ci, id);
}
int CI::removeProfile(const uint8_t id[5]) {
    return midi2_ci_remove_profile(&cst(_state)->ci, id);
}

void CI::onProfileInquiry(std::function<void()> cb) { cst(_state)->cb_profile_inquiry = std::move(cb); }
void CI::onProfileEnable(ProfileOnOffCb cb)         { cst(_state)->cb_profile_enable  = std::move(cb); }
void CI::onProfileDisable(ProfileOnOffCb cb)        { cst(_state)->cb_profile_disable = std::move(cb); }
void CI::onProfileAdded(ProfileIdCb cb)             { cst(_state)->cb_profile_added   = std::move(cb); }
void CI::onProfileRemoved(ProfileIdCb cb)           { cst(_state)->cb_profile_removed = std::move(cb); }
void CI::onProfileDetailsInquiry(ProfileDetailsCb cb) {
    cst(_state)->cb_profile_details_inquiry = std::move(cb);
}
void CI::onProfileSpecificData(ProfileDataCb cb) {
    cst(_state)->cb_profile_specific_data = std::move(cb);
}

// Property Exchange
int CI::addProperty(const char* name, PeGetter getter, PeSetter setter) {
    auto* s = cst(_state);
    int rc = midi2_ci_add_property_dynamic(&s->ci, name, &tramp_pe_getter, &tramp_pe_setter);
    if (rc == MIDI2_CI_OK) {
        // store the user lambdas in the slot just appended
        uint8_t i = (uint8_t)(s->ci.property_count - 1);
        s->pe_getters[i] = std::move(getter);
        s->pe_setters[i] = std::move(setter);
    }
    return rc;
}

int CI::addPropertyStatic(const char* name, const char* staticValue) {
    return midi2_ci_add_property_static(&cst(_state)->ci, name, staticValue);
}

int CI::setPropertySubscribable(const char* name, bool enabled) {
    return midi2_ci_pe_set_subscribable(&cst(_state)->ci, name, enabled);
}

uint8_t CI::subscriberCount() const {
    return midi2_ci_get_subscriber_count(&cst(_state)->ci);
}

void CI::notifyPropertyChanged(const char* name) {
    midi2_ci_notify_property_changed(&cst(_state)->ci, name);
}

int CI::removeProperty(const char* name) {
    auto* s = cst(_state);
    // upstream midi2_ci_remove_property shifts properties[] left to keep
    // contiguous storage (impl ~line 4669 in midi2.h). Our pe_getters[] /
    // pe_setters[] arrays are indexed in lockstep, so we must mirror the
    // shift, otherwise the wrong lambda fires for the resource that moved
    // up. Locate the slot before the upstream call decrements property_count.
    if (!name) return MIDI2_CI_ERR_NOT_FOUND;
    uint8_t found = 0xFF;
    for (uint8_t i = 0; i < s->ci.property_count; ++i) {
        if (s->ci.properties[i].name
            && std::strcmp(s->ci.properties[i].name, name) == 0) {
            found = i;
            break;
        }
    }
    int rc = midi2_ci_remove_property(&s->ci, name);
    if (rc == MIDI2_CI_OK && found != 0xFF) {
        // property_count is now post-removal; shift slots [found+1 .. count]
        // left by one, then clear the now-vacated trailing slot.
        for (uint8_t j = found; j < s->ci.property_count; ++j) {
            s->pe_getters[j] = std::move(s->pe_getters[j + 1]);
            s->pe_setters[j] = std::move(s->pe_setters[j + 1]);
        }
        s->pe_getters[s->ci.property_count] = {};
        s->pe_setters[s->ci.property_count] = {};
    }
    return rc;
}

void CI::onPECapability(PeCapsCb cb)      { cst(_state)->cb_pe_capability = std::move(cb); }
void CI::onPEGet(PeGetCb cb)              { cst(_state)->cb_pe_get        = std::move(cb); }
void CI::onPESet(PeSetCb cb)              { cst(_state)->cb_pe_set        = std::move(cb); }
void CI::onPESubscribe(PeSubscribeCb cb)  { cst(_state)->cb_pe_subscribe  = std::move(cb); }
void CI::onPENotify(PeNotifyCb cb)        { cst(_state)->cb_pe_notify     = std::move(cb); }

// Process Inquiry
void CI::setMidiReport(uint8_t msgDataControl,
                       uint64_t systemBitmap, uint64_t channelBitmap, uint64_t noteBitmap) {
    auto* s = cst(_state);
    s->midi_report_data_control  = msgDataControl;
    s->midi_report_system_bitmap = systemBitmap;
    s->midi_report_channel_bitmap = channelBitmap;
    s->midi_report_note_bitmap   = noteBitmap;
}

void CI::onPICapability(PiCapsCb cb)             { cst(_state)->cb_pi_capability = std::move(cb); }
void CI::onMidiReportInquiry(PiMidiReportCb cb)  { cst(_state)->cb_midi_report_inquiry = std::move(cb); }

}  // namespace midi2

#include "midi2_host.h"

#include <cstring>

namespace midi2 {

// ============================================================================
// Per-device dispatch context.
//
// Every per-idx midi2_dispatch + midi2_ci_dispatch carries a pointer to one
// of these as `context`. Trampolines unpack {host, idx} so a single set of
// trampoline functions serves all MAX_DEVICES instances.
// ============================================================================
struct HostDispatchContext {
    struct HostState* host;
    uint8_t           idx;
};

// ============================================================================
// SysEx7 fragmenter adapter for midi2_proc_send_sysex7.
//
// The C99 helper takes a write_fn(words, count, context) pointer. The host's
// public WriteFn takes (idx, words, count). We adapt with this small
// trampoline + per-call context.
// ============================================================================
struct SysExSendCtx {
    Host::WriteFn* write_fn;   // pointer to user's write_fn (lives in HostState)
    uint8_t        idx;
};

static uint32_t sysex_send_trampoline(const uint32_t* words, uint32_t count,
                                      void* context) {
    auto* c = static_cast<SysExSendCtx*>(context);
    if (!c || !c->write_fn || !*c->write_fn) return 0;
    (*c->write_fn)(c->idx, words, count);
    return count;
}

// ============================================================================
// HostState — internal pimpl. Per-idx arrays of midi2 C99 state, plus
// platform hooks and user callback slots.
//
// Memory footprint (MAX_DEVICES = 4):
//   per idx:  midi2_proc (~60B) + midi2_dispatch (~200B) +
//             midi2_ci_dispatch (~250B) + sysex7_buf (512B) +
//             sysex8_buf (512B) + dispatch_context (16B) ≈ 1550B
//   total:    ~6.2 KB for arrays + ~800B identities + ~700B callbacks ≈ 7.7 KB.
// ============================================================================
struct HostState {
    // Platform contract slots
    Host::WriteFn write_fn;
    Host::NowFn   now_fn;
    Host::RngFn   rng_fn;

    // Host's own MUID (CI Initiator role)
    uint32_t host_muid;

    // Per-device identity table
    Host::DeviceIdentity identities[Host::MAX_DEVICES];

    // Optional inbound group remap, per device. all-identity by default.
    bool    has_remap[Host::MAX_DEVICES];
    uint8_t remap[Host::MAX_DEVICES][16];

    // Auto-discover toggle (default true; set on construction)
    bool auto_discover;

    // Per-device midi2 C99 state — proc + dispatch + ci_dispatch.
    midi2_proc_state    procs[Host::MAX_DEVICES];
    midi2_dispatch      dispatches[Host::MAX_DEVICES];
    midi2_ci_dispatch   ci_dispatches[Host::MAX_DEVICES];
    HostDispatchContext dispatch_contexts[Host::MAX_DEVICES];

    uint8_t sysex7_bufs[Host::MAX_DEVICES][512];
    uint8_t sysex8_bufs[Host::MAX_DEVICES][512];

    // Counter for CI Discovery Inquiry request_ids (just monotonic, never 0)
    uint32_t next_discovery_request_id;

    // User-facing callbacks
    Host::DeviceConnectedCb    cb_device_connected;
    Host::DeviceDisconnectedCb cb_device_disconnected;
    Host::IdentityUpdatedCb    cb_identity_updated;

    Host::NoteCb           cb_note_on;
    Host::NoteCb           cb_note_off;
    Host::ControllerCb     cb_cc;
    Host::PitchBendCb      cb_pitch_bend;
    Host::PressureCb       cb_chan_pressure;
    Host::PolyPressureCb   cb_poly_pressure;
    Host::PerNotePbCb      cb_per_note_pb;
    Host::PerNoteCtrlCb    cb_reg_per_note;
    Host::PerNoteCtrlCb    cb_asn_per_note;
    Host::ProgramCb        cb_program;
    Host::SysEx7Cb         cb_sysex7;
    Host::SysEx8Cb         cb_sysex8;
    Host::TempoCb          cb_tempo;
    Host::TimeSigCb        cb_time_sig;
    Host::KeySigCb         cb_key_sig;
    Host::ChordCb          cb_chord;
    Host::JrTimestampCb    cb_jr_timestamp;
};

static inline HostState* st(void* p) {
    return static_cast<HostState*>(p);
}

// Helper: pack attribute_type + attribute_data into the 16-bit attribute
// field of MT 0x4 NoteOn/Off (mirrors Device).
static inline bool pack_attribute(uint8_t attrType, uint16_t attrData,
                                   uint16_t* out) {
    if (attrData > 0x00FF) return false;
    *out = (uint16_t)((attrType << 8) | (attrData & 0xFF));
    return true;
}

// ----------------------------------------------------------------------------
// C99 dispatch trampolines. Each unpacks HostDispatchContext to recover
// (host, idx) and forwards to the host-level user callback.
// ----------------------------------------------------------------------------
namespace {

// MT 0x0 Utility
void tramp_jr_timestamp(uint8_t group, uint16_t ts, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_jr_timestamp) c->host->cb_jr_timestamp(c->idx, group, ts);
}

// MT 0x4 MIDI 2.0 Channel Voice
void tramp_note_on(uint8_t g, uint8_t ch, uint8_t note, uint16_t vel,
                    uint8_t at, uint16_t ad, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_note_on) c->host->cb_note_on(c->idx, g, ch, note, vel, at, ad);
}
void tramp_note_off(uint8_t g, uint8_t ch, uint8_t note, uint16_t vel,
                     uint8_t at, uint16_t ad, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_note_off) c->host->cb_note_off(c->idx, g, ch, note, vel, at, ad);
}
void tramp_cc(uint8_t g, uint8_t ch, uint8_t i, uint32_t v, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_cc) c->host->cb_cc(c->idx, g, ch, i, v);
}
void tramp_pitch_bend(uint8_t g, uint8_t ch, uint32_t v, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_pitch_bend) c->host->cb_pitch_bend(c->idx, g, ch, v);
}
void tramp_chan_pressure(uint8_t g, uint8_t ch, uint32_t v, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_chan_pressure) c->host->cb_chan_pressure(c->idx, g, ch, v);
}
void tramp_poly_pressure(uint8_t g, uint8_t ch, uint8_t note, uint32_t v, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_poly_pressure) c->host->cb_poly_pressure(c->idx, g, ch, note, v);
}
void tramp_per_note_pb(uint8_t g, uint8_t ch, uint8_t note, uint32_t v, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_per_note_pb) c->host->cb_per_note_pb(c->idx, g, ch, note, v);
}
void tramp_reg_per_note(uint8_t g, uint8_t ch, uint8_t note, uint8_t i, uint32_t v, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_reg_per_note) c->host->cb_reg_per_note(c->idx, g, ch, note, i, v);
}
void tramp_asn_per_note(uint8_t g, uint8_t ch, uint8_t note, uint8_t i, uint32_t v, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_asn_per_note) c->host->cb_asn_per_note(c->idx, g, ch, note, i, v);
}
void tramp_program(uint8_t g, uint8_t ch, uint8_t prog, bool bv,
                    uint8_t bm, uint8_t bl, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_program) c->host->cb_program(c->idx, g, ch, prog, bm, bl, bv);
}

// MT 0xD Flex Data
void tramp_tempo(uint8_t group, uint32_t ten_ns_per_qn, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_tempo) c->host->cb_tempo(c->idx, group, ten_ns_per_qn);
}
void tramp_time_sig(uint8_t group, uint8_t num, uint8_t denom,
                     uint8_t /*num_32nd_notes*/, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_time_sig) c->host->cb_time_sig(c->idx, group, num, denom);
}
void tramp_key_sig(uint8_t group, uint8_t /*address*/, uint8_t /*channel*/,
                    int8_t sf, uint8_t /*tonic*/, uint8_t key_type, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (c->host->cb_key_sig) c->host->cb_key_sig(c->idx, group, sf, key_type == 1);
}
void tramp_chord(uint8_t group, uint8_t address, uint8_t channel,
                  int8_t tonic_sf, uint8_t tonic_note, uint8_t chord_type,
                  uint8_t a1t, uint8_t a1d, uint8_t a2t, uint8_t a2d,
                  uint8_t a3t, uint8_t a3d, uint8_t a4t, uint8_t a4d,
                  int8_t bass_sf, uint8_t bass_note, uint8_t bass_type,
                  uint8_t b1t, uint8_t b1d, uint8_t b2t, uint8_t b2d,
                  void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    if (!c->host->cb_chord) return;
    ChordDescriptor d{};
    d.address        = address;
    d.channel        = channel;
    d.tonicSharpFlat = tonic_sf;
    d.tonicNote      = tonic_note;
    d.chordType      = chord_type;
    d.alt1Type = a1t; d.alt1Degree = a1d;
    d.alt2Type = a2t; d.alt2Degree = a2d;
    d.alt3Type = a3t; d.alt3Degree = a3d;
    d.alt4Type = a4t; d.alt4Degree = a4d;
    d.bassSharpFlat = bass_sf;
    d.bassNote       = bass_note;
    d.bassChordType  = bass_type;
    d.bassAlt1Type = b1t; d.bassAlt1Degree = b1d;
    d.bassAlt2Type = b2t; d.bassAlt2Degree = b2d;
    c->host->cb_chord(c->idx, group, d);
}

// MT 0xF UMP Stream — populate identity from device's Discovery responses.
void tramp_endpoint_info(uint8_t ump_maj, uint8_t ump_min, bool static_fb,
                          uint8_t num_fb, bool midi2, bool midi1,
                          bool /*rx_jr*/, bool /*tx_jr*/, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    auto& id = c->host->identities[c->idx];
    id.umpVerMajor           = ump_maj;
    id.umpVerMinor           = ump_min;
    id.numFunctionBlocks     = num_fb;
    id.supportsMidi1Protocol = midi1;
    id.supportsMidi2Protocol = midi2;
    (void)static_fb;
    if (c->host->cb_identity_updated) c->host->cb_identity_updated(c->idx, id);
}

void tramp_device_identity(uint32_t mfr_id, uint16_t fam, uint16_t mod,
                            uint32_t ver, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    auto& id = c->host->identities[c->idx];
    // Unpack 24-bit packed manufacturer id (MSB-first, M2-104 §7.1.6).
    id.manufacturerId[0] = (uint8_t)((mfr_id >> 16) & 0xFF);
    id.manufacturerId[1] = (uint8_t)((mfr_id >>  8) & 0xFF);
    id.manufacturerId[2] = (uint8_t)( mfr_id        & 0xFF);
    id.familyId = fam;
    id.modelId  = mod;
    id.version  = ver;
    if (c->host->cb_identity_updated) c->host->cb_identity_updated(c->idx, id);
}

void tramp_stream_text(uint16_t status, uint8_t /*format*/,
                        const uint8_t* data, uint8_t len, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    auto& id = c->host->identities[c->idx];
    char* dst = nullptr;
    size_t cap = 0;
    if (status == 0x03)      { dst = id.endpointName;       cap = sizeof(id.endpointName); }
    else if (status == 0x04) { dst = id.productInstanceId;  cap = sizeof(id.productInstanceId); }
    else                     { return; }   // FB Name uses tramp_fb_name below
    if (!dst) return;
    size_t n = (len < cap - 1) ? len : (cap - 1);
    std::memcpy(dst, data, n);
    dst[n] = '\0';
    if (c->host->cb_identity_updated) c->host->cb_identity_updated(c->idx, id);
}

void tramp_fb_info(bool active, uint8_t fb_num, uint8_t /*direction*/,
                    uint8_t /*first_group*/, uint8_t /*num_groups*/,
                    uint8_t /*midi_ci_ver*/, uint8_t /*max_sysex8_streams*/,
                    uint8_t /*protocol*/, void* ctx) {
    // For v0.1 we only track the FB count via tramp_endpoint_info. Detailed
    // per-FB topology lands when the bridge example needs it.
    (void)active; (void)fb_num; (void)ctx;
}

void tramp_fb_name(uint8_t /*format*/, uint8_t /*fb_num*/,
                    const uint8_t* /*data*/, uint8_t /*len*/, void* /*ctx*/) {
    // Same — v0.1 tracks endpoint name only. Per-FB names come later.
}

// ----------------------------------------------------------------------------
// SysEx7 reassembly hook — when a complete CI SysEx arrives, route it to
// the per-idx midi2_ci_dispatch so on_discovery_reply etc. fire.
//
// Two-hop context: proc.context points at dispatch (so on_ump =
// midi2_dispatch_feed works). The dispatch's own context field is
// dispatch_contexts[idx]. We recover (host, idx) via that second hop.
// ----------------------------------------------------------------------------
void tramp_proc_sysex7(uint8_t group, const uint8_t* data, uint16_t len,
                        void* ctx) {
    auto* d = static_cast<midi2_dispatch*>(ctx);
    auto* c = static_cast<HostDispatchContext*>(d->context);
    auto* host = c->host;

    // Detect MIDI-CI: Universal SysEx Non-Real-Time sub-id1 0x0D.
    // Format: F0 7E <device_id> 0D <sub_id_2> ...  but proc strips F0/F7,
    // so we receive 7E <dev> 0D <sub> ...
    if (len >= 4 && data[0] == 0x7E && data[2] == 0x0D) {
        midi2_ci_dispatch_feed(&host->ci_dispatches[c->idx], group, data, len);
    }

    // Always fire the user-facing SysEx7 callback regardless of CI content,
    // so apps that need raw access (logging, custom protocols) still see it.
    if (host->cb_sysex7) host->cb_sysex7(c->idx, group, data, len);
}

void tramp_proc_sysex8(uint8_t group, uint8_t stream_id, const uint8_t* data,
                        uint16_t len, void* ctx) {
    auto* d = static_cast<midi2_dispatch*>(ctx);
    auto* c = static_cast<HostDispatchContext*>(d->context);
    if (c->host->cb_sysex8) c->host->cb_sysex8(c->idx, group, stream_id, data, len);
}

// ----------------------------------------------------------------------------
// MIDI-CI dispatch trampoline — handle Discovery Reply: validate the
// request_id matches our pending inquiry, then populate identity.
// ----------------------------------------------------------------------------
void tramp_ci_discovery_reply(midi2_ci_header hdr, uint32_t mfr_id,
                               uint16_t family, uint16_t model,
                               uint32_t sw_rev, uint8_t /*ci_category*/,
                               uint32_t /*max_sysex*/,
                               uint8_t /*output_path_id*/,
                               uint8_t /*function_block*/, void* ctx) {
    auto* c = static_cast<HostDispatchContext*>(ctx);
    auto& id = c->host->identities[c->idx];

    // We are the Initiator. Validate dst_muid matches our host_muid (it
    // should — the device is replying to us). src_muid is the device's MUID.
    if (hdr.dst_muid != c->host->host_muid) return;

    id.ciMuid                 = hdr.src_muid;
    id.ciDiscovered           = true;
    id.ciDiscoveryPending     = false;
    id.ciDiscoveryRequestId   = 0;
    // Also populate the same fields Device Identity notification populates,
    // so identity is coherent regardless of which arrives first.
    id.manufacturerId[0] = (uint8_t)((mfr_id >> 16) & 0xFF);
    id.manufacturerId[1] = (uint8_t)((mfr_id >>  8) & 0xFF);
    id.manufacturerId[2] = (uint8_t)( mfr_id        & 0xFF);
    id.familyId = family;
    id.modelId  = model;
    id.version  = sw_rev;

    if (c->host->cb_identity_updated) c->host->cb_identity_updated(c->idx, id);
}

}  // anonymous namespace

// ----------------------------------------------------------------------------
// Construction / destruction
// ----------------------------------------------------------------------------

Host::Host() {
    auto* s = new HostState{};
    s->host_muid                 = 0;
    s->auto_discover             = true;
    s->next_discovery_request_id = 1;
    _state = s;
}

Host::~Host() {
    delete st(_state);
}

void Host::begin() {
    auto* s = st(_state);

    if (s->rng_fn) {
        s->host_muid = s->rng_fn() & 0x0FFFFFFFu;  // 28-bit MUID per M2-101
    }

    for (uint8_t i = 0; i < MAX_DEVICES; ++i) {
        // Per-idx dispatch context so trampolines recover (host, idx).
        s->dispatch_contexts[i].host = s;
        s->dispatch_contexts[i].idx  = i;

        midi2_proc_init(&s->procs[i],
                        s->sysex7_bufs[i], sizeof(s->sysex7_bufs[i]),
                        s->sysex8_bufs[i], sizeof(s->sysex8_bufs[i]));

        midi2_dispatch_init(&s->dispatches[i]);
        s->dispatches[i].context = &s->dispatch_contexts[i];

        // Wire MT 0x0
        s->dispatches[i].on_jr_timestamp     = tramp_jr_timestamp;
        // Wire MT 0x4
        s->dispatches[i].on_note_on          = tramp_note_on;
        s->dispatches[i].on_note_off         = tramp_note_off;
        s->dispatches[i].on_poly_pressure    = tramp_poly_pressure;
        s->dispatches[i].on_cc               = tramp_cc;
        s->dispatches[i].on_program          = tramp_program;
        s->dispatches[i].on_chan_pressure    = tramp_chan_pressure;
        s->dispatches[i].on_pitch_bend       = tramp_pitch_bend;
        s->dispatches[i].on_per_note_pb      = tramp_per_note_pb;
        s->dispatches[i].on_reg_per_note     = tramp_reg_per_note;
        s->dispatches[i].on_asn_per_note     = tramp_asn_per_note;
        // Wire MT 0xD
        s->dispatches[i].on_tempo            = tramp_tempo;
        s->dispatches[i].on_time_sig         = tramp_time_sig;
        s->dispatches[i].on_key_sig          = tramp_key_sig;
        s->dispatches[i].on_chord            = tramp_chord;
        // Wire MT 0xF (UMP Stream from device)
        s->dispatches[i].on_endpoint_info    = tramp_endpoint_info;
        s->dispatches[i].on_device_identity  = tramp_device_identity;
        s->dispatches[i].on_stream_text      = tramp_stream_text;
        s->dispatches[i].on_fb_info          = tramp_fb_info;
        s->dispatches[i].on_fb_name          = tramp_fb_name;

        // proc -> dispatch (UMP) + proc -> SysEx7/8 reassembly hooks.
        // proc.context is what midi2_dispatch_feed receives as its third
        // argument, so it must point at the dispatch struct itself. The
        // SysEx7/8 trampolines we install below get the same context, so
        // they unpack from there to recover (host, idx).
        //
        // Two-hop addressing trick: we want proc -> dispatch_feed -> our
        // trampolines, AND proc -> our SysEx hooks. The dispatch struct
        // already carries dispatch_contexts[i] in its own .context field
        // (set above), so the dispatch trampolines see the right context.
        // For SysEx hooks we use proc.context — since proc.on_ump consumes
        // it as dispatch*, but proc.on_sysex7/8 also consume it, we need
        // to reach dispatch_contexts[i] from the SysEx side too. The
        // simplest way is to point proc.context at the dispatch struct
        // (so dispatch_feed works) and unpack dispatch_contexts via the
        // dispatch's own context inside the SysEx trampoline.
        s->procs[i].context   = &s->dispatches[i];
        s->procs[i].on_ump    = midi2_dispatch_feed;
        s->procs[i].on_sysex7 = tramp_proc_sysex7;
        s->procs[i].on_sysex8 = tramp_proc_sysex8;

        // Per-idx CI dispatch (for parsing CI replies)
        midi2_ci_dispatch_init(&s->ci_dispatches[i]);
        s->ci_dispatches[i].context           = &s->dispatch_contexts[i];
        s->ci_dispatches[i].on_discovery_reply = tramp_ci_discovery_reply;
    }
}

void Host::task() {
    // CI Discovery timeout housekeeping. If a device's inquiry has been
    // pending for > 2 seconds and the now_fn is wired, mark it timed out.
    auto* s = st(_state);
    if (!s->now_fn) return;
    constexpr uint32_t kDiscoveryTimeoutMs = 2000;
    uint32_t now = s->now_fn();
    for (uint8_t i = 0; i < MAX_DEVICES; ++i) {
        auto& id = s->identities[i];
        if (!id.mounted || !id.ciDiscoveryPending) continue;
        if (now - id.ciDiscoverySentMs >= kDiscoveryTimeoutMs) {
            id.ciDiscoveryPending   = false;
            id.ciDiscoveryRequestId = 0;
            // Note: ciDiscovered stays false. App can retry via
            // sendDiscoveryInquiry(idx).
        }
    }
}

// ----------------------------------------------------------------------------
// Platform contract setters
// ----------------------------------------------------------------------------

void Host::setWriteFn(WriteFn fn) { st(_state)->write_fn = std::move(fn); }
void Host::setNowFn(NowFn fn)     { st(_state)->now_fn   = std::move(fn); }
void Host::setRngFn(RngFn fn)     { st(_state)->rng_fn   = std::move(fn); }

void Host::feedRx(uint8_t idx, const uint32_t* words, size_t count) {
    if (idx >= MAX_DEVICES) return;
    if (!words || count == 0) return;
    auto* s = st(_state);
    // midi2_proc_feed takes uint8_t word_count; chunk for safety. Apply
    // optional inbound group remap by rewriting the group nibble of MT
    // 0x2..0x5 word0 before feeding the proc.
    while (count > 0) {
        uint8_t chunk = (count > 64) ? 64 : (uint8_t)count;
        if (s->has_remap[idx]) {
            uint32_t rewritten[64];
            for (uint8_t i = 0; i < chunk; ++i) {
                uint32_t w = words[i];
                uint8_t mt = (uint8_t)((w >> 28) & 0xF);
                if (mt >= 0x2 && mt <= 0x5) {
                    uint8_t old_g = (uint8_t)((w >> 24) & 0xF);
                    uint8_t new_g = s->remap[idx][old_g];
                    w = (w & 0xF0FFFFFFu) | ((uint32_t)new_g << 24);
                }
                rewritten[i] = w;
            }
            midi2_proc_feed(&s->procs[idx], rewritten, chunk);
        } else {
            midi2_proc_feed(&s->procs[idx], words, chunk);
        }
        words += chunk;
        count -= chunk;
    }
}

void Host::notifyDeviceMounted(uint8_t idx,
                                uint8_t protocolVersion,
                                uint8_t cableCount,
                                uint8_t altSettingActive,
                                uint16_t bcdMSC) {
    if (idx >= MAX_DEVICES) return;
    auto* s = st(_state);
    auto& id = s->identities[idx];

    // Reset identity but keep the dispatch wiring intact (it lives in
    // s->dispatches[idx], not in id).
    id                   = DeviceIdentity{};
    id.mounted           = true;
    id.protocolVersion   = protocolVersion;
    id.cableCount        = cableCount;
    id.altSettingActive  = altSettingActive;
    id.bcdMSC            = bcdMSC;

    if (s->cb_device_connected) s->cb_device_connected(idx, id);

    if (s->auto_discover) {
        // Send UMP Stream Endpoint Discovery: ask for all 5 notification
        // categories (Endpoint Info / Device Identity / Endpoint Name /
        // Product Instance ID / Stream Config). filter = 0x1F covers them.
        if (s->write_fn) {
            uint32_t words[4] = {0};
            midi2_msg_stream_endpoint_discovery(words, /*ump_ver_major*/ 1,
                                                  /*ump_ver_minor*/ 1,
                                                  /*filter*/ 0x1F);
            s->write_fn(idx, words, 4);

            // Also FB Discovery for FB 0xFF (all FBs) — info + name bits.
            uint32_t fbw[4] = {0};
            midi2_msg_stream_fb_discovery(fbw, /*fb_num*/ 0xFF,
                                            /*filter*/ 0x03);
            s->write_fn(idx, fbw, 4);
        }
        // CI Discovery Inquiry — populates ciMuid and the manufacturer
        // bundle when the device replies.
        sendDiscoveryInquiry(idx);
    }
}

void Host::notifyDeviceUnmounted(uint8_t idx) {
    if (idx >= MAX_DEVICES) return;
    auto* s = st(_state);
    s->identities[idx].mounted = false;
    if (s->cb_device_disconnected) s->cb_device_disconnected(idx);
}

// ----------------------------------------------------------------------------
// Queries
// ----------------------------------------------------------------------------

uint8_t Host::deviceCount() const {
    auto* s = st(_state);
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_DEVICES; ++i) if (s->identities[i].mounted) ++n;
    return n;
}

bool Host::isDeviceMounted(uint8_t idx) const {
    if (idx >= MAX_DEVICES) return false;
    return st(_state)->identities[idx].mounted;
}

const Host::DeviceIdentity& Host::identity(uint8_t idx) const {
    static const DeviceIdentity empty{};
    if (idx >= MAX_DEVICES) return empty;
    return st(_state)->identities[idx];
}

uint32_t Host::hostMuid() const { return st(_state)->host_muid; }

void Host::regenerateHostMuid() {
    auto* s = st(_state);
    if (s->rng_fn) s->host_muid = s->rng_fn() & 0x0FFFFFFFu;
}

// ----------------------------------------------------------------------------
// Lifecycle callback setters
// ----------------------------------------------------------------------------

void Host::onDeviceConnected(DeviceConnectedCb cb)       { st(_state)->cb_device_connected    = std::move(cb); }
void Host::onDeviceDisconnected(DeviceDisconnectedCb cb) { st(_state)->cb_device_disconnected = std::move(cb); }
void Host::onIdentityUpdated(IdentityUpdatedCb cb)       { st(_state)->cb_identity_updated    = std::move(cb); }

// ----------------------------------------------------------------------------
// Inbound dispatch callback setters (verbose direct, simple form wraps).
// ----------------------------------------------------------------------------
void Host::onNoteOn(NoteCb cb)  { st(_state)->cb_note_on  = std::move(cb); }
void Host::onNoteOn(NoteSimpleCb cb) {
    st(_state)->cb_note_on =
        [cb = std::move(cb)](uint8_t idx, uint8_t /*g*/, uint8_t ch,
                              uint8_t note, uint16_t vel,
                              uint8_t /*at*/, uint16_t /*ad*/) {
            if (cb) cb(idx, ch, note, vel);
        };
}
void Host::onNoteOff(NoteCb cb) { st(_state)->cb_note_off = std::move(cb); }
void Host::onNoteOff(NoteSimpleCb cb) {
    st(_state)->cb_note_off =
        [cb = std::move(cb)](uint8_t idx, uint8_t /*g*/, uint8_t ch,
                              uint8_t note, uint16_t vel,
                              uint8_t /*at*/, uint16_t /*ad*/) {
            if (cb) cb(idx, ch, note, vel);
        };
}
void Host::onCC(ControllerCb cb) { st(_state)->cb_cc = std::move(cb); }
void Host::onCC(ControllerSimpleCb cb) {
    st(_state)->cb_cc =
        [cb = std::move(cb)](uint8_t idx, uint8_t /*g*/, uint8_t ch,
                              uint8_t i, uint32_t v) {
            if (cb) cb(idx, ch, i, v);
        };
}
void Host::onPitchBend(PitchBendCb cb) { st(_state)->cb_pitch_bend = std::move(cb); }
void Host::onPitchBend(PitchBendSimpleCb cb) {
    st(_state)->cb_pitch_bend =
        [cb = std::move(cb)](uint8_t idx, uint8_t /*g*/, uint8_t ch, uint32_t v) {
            if (cb) cb(idx, ch, v);
        };
}
void Host::onChannelPressure(PressureCb cb)        { st(_state)->cb_chan_pressure = std::move(cb); }
void Host::onChannelPressure(PressureSimpleCb cb) {
    st(_state)->cb_chan_pressure =
        [cb = std::move(cb)](uint8_t idx, uint8_t /*g*/, uint8_t ch, uint32_t v) {
            if (cb) cb(idx, ch, v);
        };
}
void Host::onPolyPressure(PolyPressureCb cb)        { st(_state)->cb_poly_pressure = std::move(cb); }
void Host::onPolyPressure(PolyPressureSimpleCb cb) {
    st(_state)->cb_poly_pressure =
        [cb = std::move(cb)](uint8_t idx, uint8_t /*g*/, uint8_t ch,
                              uint8_t note, uint32_t v) {
            if (cb) cb(idx, ch, note, v);
        };
}
void Host::onPerNotePitchBend(PerNotePbCb cb)        { st(_state)->cb_per_note_pb  = std::move(cb); }
void Host::onRegPerNoteController(PerNoteCtrlCb cb)  { st(_state)->cb_reg_per_note = std::move(cb); }
void Host::onAsnPerNoteController(PerNoteCtrlCb cb)  { st(_state)->cb_asn_per_note = std::move(cb); }
void Host::onProgram(ProgramCb cb)                    { st(_state)->cb_program     = std::move(cb); }
void Host::onSysEx7(SysEx7Cb cb)                      { st(_state)->cb_sysex7      = std::move(cb); }
void Host::onSysEx8(SysEx8Cb cb)                      { st(_state)->cb_sysex8      = std::move(cb); }
void Host::onTempo(TempoCb cb)                        { st(_state)->cb_tempo       = std::move(cb); }
void Host::onTimeSignature(TimeSigCb cb)              { st(_state)->cb_time_sig    = std::move(cb); }
void Host::onKeySignature(KeySigCb cb)                { st(_state)->cb_key_sig     = std::move(cb); }
void Host::onChord(ChordCb cb)                        { st(_state)->cb_chord       = std::move(cb); }
void Host::onJRTimestamp(JrTimestampCb cb)            { st(_state)->cb_jr_timestamp = std::move(cb); }

// ----------------------------------------------------------------------------
// Outbound senders. Build UMP via midi2_msg_*; dispatch via write_fn(idx).
// Returns false on out-of-range idx, unmounted device, missing write_fn,
// or invalid arguments.
// ----------------------------------------------------------------------------
bool Host::sendNoteOn(uint8_t idx, uint8_t group, uint8_t channel,
                      uint8_t note, uint16_t velocity,
                      uint8_t attrType, uint16_t attrData) {
    if (idx >= MAX_DEVICES) return false;
    auto* s = st(_state);
    if (!s->identities[idx].mounted || !s->write_fn) return false;
    uint16_t attribute;
    if (!pack_attribute(attrType, attrData, &attribute)) return false;
    uint32_t words[2];
    midi2_msg_note_on(words, group, channel, note, velocity, attribute);
    s->write_fn(idx, words, 2);
    return true;
}
bool Host::sendNoteOff(uint8_t idx, uint8_t group, uint8_t channel,
                        uint8_t note, uint16_t velocity,
                        uint8_t attrType, uint16_t attrData) {
    if (idx >= MAX_DEVICES) return false;
    auto* s = st(_state);
    if (!s->identities[idx].mounted || !s->write_fn) return false;
    uint16_t attribute;
    if (!pack_attribute(attrType, attrData, &attribute)) return false;
    uint32_t words[2];
    midi2_msg_note_off(words, group, channel, note, velocity, attribute);
    s->write_fn(idx, words, 2);
    return true;
}
bool Host::sendCC(uint8_t idx, uint8_t group, uint8_t channel,
                  uint8_t index, uint32_t value) {
    if (idx >= MAX_DEVICES) return false;
    auto* s = st(_state);
    if (!s->identities[idx].mounted || !s->write_fn) return false;
    uint32_t words[2];
    midi2_msg_cc(words, group, channel, index, value);
    s->write_fn(idx, words, 2);
    return true;
}
bool Host::sendPitchBend(uint8_t idx, uint8_t group, uint8_t channel,
                         uint32_t value) {
    if (idx >= MAX_DEVICES) return false;
    auto* s = st(_state);
    if (!s->identities[idx].mounted || !s->write_fn) return false;
    uint32_t words[2];
    midi2_msg_pitch_bend(words, group, channel, value);
    s->write_fn(idx, words, 2);
    return true;
}
bool Host::sendChannelPressure(uint8_t idx, uint8_t group, uint8_t channel,
                                uint32_t value) {
    if (idx >= MAX_DEVICES) return false;
    auto* s = st(_state);
    if (!s->identities[idx].mounted || !s->write_fn) return false;
    uint32_t words[2];
    midi2_msg_chan_pressure(words, group, channel, value);
    s->write_fn(idx, words, 2);
    return true;
}
bool Host::sendPolyPressure(uint8_t idx, uint8_t group, uint8_t channel,
                             uint8_t note, uint32_t value) {
    if (idx >= MAX_DEVICES) return false;
    auto* s = st(_state);
    if (!s->identities[idx].mounted || !s->write_fn) return false;
    uint32_t words[2];
    midi2_msg_poly_pressure(words, group, channel, note, value);
    s->write_fn(idx, words, 2);
    return true;
}

bool Host::noteOn(uint8_t idx, uint8_t channel, uint8_t note, uint16_t velocity) {
    return sendNoteOn(idx, /*group*/ 0, channel, note, velocity);
}
bool Host::noteOff(uint8_t idx, uint8_t channel, uint8_t note, uint16_t velocity) {
    return sendNoteOff(idx, /*group*/ 0, channel, note, velocity);
}
bool Host::cc(uint8_t idx, uint8_t channel, uint8_t index, uint32_t value) {
    return sendCC(idx, /*group*/ 0, channel, index, value);
}
bool Host::pitchBend(uint8_t idx, uint8_t channel, uint32_t value) {
    return sendPitchBend(idx, /*group*/ 0, channel, value);
}

// ----------------------------------------------------------------------------
// CI Initiator — sendDiscoveryInquiry builds a Universal SysEx CI
// Discovery, fragments it through midi2_proc_send_sysex7, and sets the
// pending-tracking on the per-device identity.
//
// Manufacturer ID for the host's own identity in the inquiry is the
// MIDI Association educational prefix 0x7D 0x00 0x00 (M2-101 §5).
// ----------------------------------------------------------------------------
bool Host::sendDiscoveryInquiry(uint8_t idx) {
    if (idx >= MAX_DEVICES) return false;
    auto* s = st(_state);
    if (!s->identities[idx].mounted || !s->write_fn) return false;

    // Allocate a request id (never zero).
    uint32_t request_id = s->next_discovery_request_id++;
    if (s->next_discovery_request_id == 0) s->next_discovery_request_id = 1;

    // Build the Discovery Inquiry SysEx.
    uint8_t buf[64];
    uint16_t len = midi2_ci_build_discovery(
        buf,
        /*version*/        MIDI2_CI_VERSION_2,
        /*src_muid*/       s->host_muid,
        /*mfr_id*/         0x7D0000u,         // educational prefix
        /*family*/         0x0001,
        /*model*/          0x0001,
        /*sw_rev*/         0x00010000,
        /*ci_category*/    0x1C,              // Profile + PE + PI
        /*max_sysex*/      512,
        /*output_path_id*/ 0);

    // Fragment + send via SysEx7 helper, adapted to write_fn(idx, ...).
    SysExSendCtx send_ctx{ &s->write_fn, idx };
    midi2_proc_send_sysex7(/*group*/ 0, buf, len,
                            &sysex_send_trampoline, &send_ctx);

    // Mark pending (caller's now_fn drives timeouts in task()).
    auto& id = s->identities[idx];
    id.ciDiscoveryPending   = true;
    id.ciDiscoveryRequestId = request_id;
    id.ciDiscoverySentMs    = s->now_fn ? s->now_fn() : 0;
    return true;
}

void Host::setAutoDiscover(bool enabled) {
    st(_state)->auto_discover = enabled;
}

// ----------------------------------------------------------------------------
// Group remap
// ----------------------------------------------------------------------------
void Host::setInboundGroupRemap(uint8_t idx, const uint8_t map[16]) {
    if (idx >= MAX_DEVICES || !map) return;
    auto* s = st(_state);
    s->has_remap[idx] = true;
    std::memcpy(s->remap[idx], map, 16);
}

void Host::clearInboundGroupRemap(uint8_t idx) {
    if (idx >= MAX_DEVICES) return;
    st(_state)->has_remap[idx] = false;
}

}  // namespace midi2

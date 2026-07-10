#include "midi2_device.h"

namespace midi2 {

// Internal device state. Single allocation owned by Device; opaque pimpl
// in the header. Holds midi2 C99 transport/dispatch state plus C++
// std::function callback storage. Each std::function with SBO is ~32 bytes;
// 49 callbacks × 32 = ~1.5 KB. Acceptable on RP2040/Teensy/ESP32 (≥256 KB
// RAM). AVR Uno (2 KB) is out of scope for v0.1.
struct DeviceState {
    midi2_proc_state proc;
    midi2_dispatch   dispatch;
    uint8_t          sysex7_buf[512];
    uint8_t          sysex8_buf[512];
    bool             mounted;
    uint8_t          alt_setting;
    uint32_t         jr_heartbeat_interval_ms;
    uint32_t         jr_last_heartbeat_ms;
    uint16_t         jr_timestamp;
    bool             jr_heartbeat_initialized;  // emit once on first task() after enable

    // Platform contract hooks (set by caller before/after begin()):
    //   write_fn — outbound UMP (forwarded by every sendXxx)
    //   now_fn   — monotonic ms clock (used only by JR heartbeat)
    Device::WriteFn  write_fn;
    Device::NowFn    now_fn;

    // ---- Callback storage (registered via Device::onXxx setters) ----
    // MT 0x0
    Device::NoArgCb                       cb_noop;
    Device::UtilityCb                     cb_jr_clock;
    Device::UtilityCb                     cb_jr_timestamp;
    std::function<void(uint16_t)>         cb_dctpq;
    std::function<void(uint32_t)>         cb_delta_clockstamp;
    // MT 0x1
    Device::SystemCb                      cb_system;
    // MT 0x2
    Device::Note1Cb                       cb_note_on1;
    Device::Note1Cb                       cb_note_off1;
    Device::Controller1Cb                 cb_cc1;
    Device::Program1Cb                    cb_program1;
    Device::PitchBend1Cb                  cb_pitch_bend1;
    Device::Pressure1Cb                   cb_chan_pressure1;
    Device::PolyPressure1Cb               cb_poly_pressure1;
    // MT 0x3
    Device::SysEx7Cb                      cb_sysex7;
    // Internal SysEx7 hook used by class CI (registered via friend access).
    // Fires after cb_sysex7 so the user-facing callback still sees the data.
    Device::SysEx7Cb                      cb_sysex7_ci;
    // MT 0x4
    Device::NoteCb                        cb_note_on;
    Device::NoteCb                        cb_note_off;
    Device::PolyPressureCb32              cb_poly_pressure;
    Device::ControllerCb32                cb_cc;
    Device::ProgramCb                     cb_program;
    Device::PressureCb32                  cb_chan_pressure;
    Device::Pb32Cb                        cb_pitch_bend;
    Device::RpnNrpnCb                     cb_rpn;
    Device::RpnNrpnCb                     cb_nrpn;
    Device::RelRpnNrpnCb                  cb_rel_rpn;
    Device::RelRpnNrpnCb                  cb_rel_nrpn;
    Device::PerNotePbCb                   cb_per_note_pb;
    Device::PerNoteCtrlCb                 cb_reg_per_note;
    Device::PerNoteCtrlCb                 cb_asn_per_note;
    Device::PerNoteMgmtCb                 cb_per_note_mgmt;
    // MT 0x5
    Device::SysEx8Cb                      cb_sysex8;
    // MT 0xD
    Device::TempoCb                       cb_tempo;
    Device::TimeSigCb                     cb_time_sig;
    Device::MetronomeCb                   cb_metronome;
    Device::KeySigCb                      cb_key_sig;
    Device::ChordCb                       cb_chord;
    Device::FlexTextCb                    cb_flex_text;
    // MT 0xF
    Device::EndpointDiscoveryCb           cb_endpoint_discovery;
    Device::EndpointInfoCb                cb_endpoint_info;
    Device::DeviceIdentityCb              cb_device_identity;
    Device::StreamTextCb                  cb_stream_text;
    Device::FbNameCb                      cb_fb_name;
    Device::StreamConfigCb                cb_stream_config_request;
    Device::StreamConfigCb                cb_stream_config_notify;
    Device::FbDiscoveryCb                 cb_fb_discovery;
    Device::FbInfoCb                      cb_fb_info;
    Device::ClipCb                        cb_clip;
};

static inline DeviceState* st(void* p) {
    return static_cast<DeviceState*>(p);
}

// Adapts the bool device_write helper to the C99 midi2_proc_write_fn type.
// Used by SysEx and UMP Stream senders that fragment internally.
static uint32_t device_write_trampoline(const uint32_t* words, uint32_t count, void* context);

// Routes outgoing UMP through the caller-supplied write_fn. Returns true if
// the function accepted the words, false if no write_fn was set. The
// platform decides what "accepted" means: a TinyUSB-backed binding can use
// the captured context to track back-pressure and signal it back via the
// bool return of sendXxx(). The library itself stays platform-agnostic.
static bool device_write(DeviceState* s, const uint32_t* words, size_t n) {
    if (!s->write_fn) return false;
    s->write_fn(words, n);
    return true;
}

// Platform contract setters
void Device::setWriteFn(WriteFn fn) {
    st(_state)->write_fn = std::move(fn);
}

void Device::setNowFn(NowFn fn) {
    st(_state)->now_fn = std::move(fn);
}

void Device::setMounted(bool mounted) {
    st(_state)->mounted = mounted;
}

void Device::setAltSetting(uint8_t alt) {
    st(_state)->alt_setting = alt;
}

void Device::feedRx(const uint32_t* words, size_t count) {
    auto* s = st(_state);
    // midi2_proc_feed consumes exactly ONE UMP packet per call: it decodes
    // the front message at words[0] and ignores anything after it. Platform
    // RX paths hand this method bursts (a USB FIFO drain routinely carries
    // several packets back-to-back), so slice the stream per packet here.
    // Feeding a multi-packet burst in one call silently drops every packet
    // after the first, which truncates a multi-packet SysEx7 run (e.g. a
    // paginated PE GET) into a bogus 404.
    size_t i = 0;
    while (i < count) {
        uint8_t mt = (uint8_t)((words[i] >> 28) & 0x0F);
        uint8_t wc = midi2_msg_word_count(mt);
        if (wc == 0 || i + wc > count) break;  // malformed or truncated tail
        midi2_proc_feed(&s->proc, words + i, wc);
        i += wc;
    }
}

midi2_proc_state* Device::procState() {
    return &st(_state)->proc;
}

// Returns 0 if no clock function is installed. The JR heartbeat path checks
// for this and skips firing — keeping the library safe to use on bare hosts
// (unit tests, desktop tools) where no clock is wired.
static uint32_t platform_now_ms(DeviceState* s) {
    return s->now_fn ? s->now_fn() : 0;
}

static uint32_t device_write_trampoline(const uint32_t* words, uint32_t count, void* context) {
    auto* s = static_cast<DeviceState*>(context);
    return device_write(s, words, count) ? count : 0;
}

// ============================================================================
// C99 dispatch -> C++ std::function trampolines
//
// midi2_dispatch's callbacks are plain C function pointers. We register one
// trampoline per slot and shuttle the call to the user's std::function when
// installed. The DeviceState* is passed via dispatch.context, set in begin().
// Trampolines that lose information (e.g. on_chord packs 21 args back into
// our ChordDescriptor) are kept symmetric with the wrapper API.
// ============================================================================

namespace {

// MT 0x0 Utility
void tramp_noop(void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_noop) s->cb_noop(0);
}
// MT 0x0 Utility is Groupless per M2-104-UM v1.1.2 section 1.4: the
// callbacks below receive only the timestamp from midi2 v0.4.0+. The
// wrapper keeps its public JrClockCb / JrTimestampCb signatures with a
// leading uint8_t group (always 0) so existing recipes keep compiling.
void tramp_jr_clock(uint16_t ts, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_jr_clock) s->cb_jr_clock(/*group*/ 0, ts);
}
void tramp_jr_timestamp(uint16_t ts, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_jr_timestamp) s->cb_jr_timestamp(/*group*/ 0, ts);
}
void tramp_dctpq(uint16_t tpq, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_dctpq) s->cb_dctpq(tpq);
}
void tramp_dc(uint32_t ticks, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_delta_clockstamp) s->cb_delta_clockstamp(ticks);
}

// MT 0x1 System
void tramp_system(uint8_t group, uint8_t status, uint8_t d1, uint8_t d2, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_system) s->cb_system(group, status, d1, d2);
}

// MT 0x2 MIDI 1.0 Channel Voice
void tramp_cv1_note_on(uint8_t g, uint8_t ch, uint8_t note, uint8_t vel, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_note_on1) s->cb_note_on1(g, ch, note, vel);
}
void tramp_cv1_note_off(uint8_t g, uint8_t ch, uint8_t note, uint8_t vel, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_note_off1) s->cb_note_off1(g, ch, note, vel);
}
void tramp_cv1_poly_pressure(uint8_t g, uint8_t ch, uint8_t note, uint8_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_poly_pressure1) s->cb_poly_pressure1(g, ch, note, v);
}
void tramp_cv1_cc(uint8_t g, uint8_t ch, uint8_t idx, uint8_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_cc1) s->cb_cc1(g, ch, idx, v);
}
void tramp_cv1_program(uint8_t g, uint8_t ch, uint8_t prog, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_program1) s->cb_program1(g, ch, prog);
}
void tramp_cv1_chan_pressure(uint8_t g, uint8_t ch, uint8_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_chan_pressure1) s->cb_chan_pressure1(g, ch, v);
}
void tramp_cv1_pitch_bend(uint8_t g, uint8_t ch, uint16_t v14, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_pitch_bend1) s->cb_pitch_bend1(g, ch, v14);
}

// MT 0x3 SysEx7: per-packet dispatch is intentionally not registered in
// begin(). The wrapper only exposes the reassembled path (proc.on_sysex7
// -> tramp_proc_sysex7 below). Users wanting per-packet access can install
// their own dispatch.on_sysex7 directly via the C99 API.

// MT 0x4 MIDI 2.0 Channel Voice
void tramp_note_on(uint8_t g, uint8_t ch, uint8_t note, uint16_t vel,
                   uint8_t at, uint16_t ad, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_note_on) s->cb_note_on(g, ch, note, vel, at, ad);
}
void tramp_note_off(uint8_t g, uint8_t ch, uint8_t note, uint16_t vel,
                    uint8_t at, uint16_t ad, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_note_off) s->cb_note_off(g, ch, note, vel, at, ad);
}
void tramp_poly_pressure(uint8_t g, uint8_t ch, uint8_t note, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_poly_pressure) s->cb_poly_pressure(g, ch, note, v);
}
void tramp_cc(uint8_t g, uint8_t ch, uint8_t idx, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_cc) s->cb_cc(g, ch, idx, v);
}
void tramp_program(uint8_t g, uint8_t ch, uint8_t prog, bool bv,
                   uint8_t bm, uint8_t bl, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_program) s->cb_program(g, ch, prog, bm, bl, bv);
}
void tramp_chan_pressure(uint8_t g, uint8_t ch, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_chan_pressure) s->cb_chan_pressure(g, ch, v);
}
void tramp_pitch_bend(uint8_t g, uint8_t ch, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_pitch_bend) s->cb_pitch_bend(g, ch, v);
}
void tramp_per_note_pb(uint8_t g, uint8_t ch, uint8_t note, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_per_note_pb) s->cb_per_note_pb(g, ch, note, v);
}
void tramp_reg_per_note(uint8_t g, uint8_t ch, uint8_t note, uint8_t idx, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_reg_per_note) s->cb_reg_per_note(g, ch, note, idx, v);
}
void tramp_asn_per_note(uint8_t g, uint8_t ch, uint8_t note, uint8_t idx, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_asn_per_note) s->cb_asn_per_note(g, ch, note, idx, v);
}
void tramp_rpn(uint8_t g, uint8_t ch, uint8_t bank, uint8_t idx, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_rpn) s->cb_rpn(g, ch, bank, idx, v);
}
void tramp_nrpn(uint8_t g, uint8_t ch, uint8_t bank, uint8_t idx, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_nrpn) s->cb_nrpn(g, ch, bank, idx, v);
}
void tramp_rel_rpn(uint8_t g, uint8_t ch, uint8_t bank, uint8_t idx, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_rel_rpn) s->cb_rel_rpn(g, ch, bank, idx, (int32_t)v);
}
void tramp_rel_nrpn(uint8_t g, uint8_t ch, uint8_t bank, uint8_t idx, uint32_t v, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_rel_nrpn) s->cb_rel_nrpn(g, ch, bank, idx, (int32_t)v);
}
void tramp_per_note_mgmt(uint8_t g, uint8_t ch, uint8_t note, bool detach, bool reset, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_per_note_mgmt) s->cb_per_note_mgmt(g, ch, note, detach, reset);
}

// MT 0xD Flex Data
void tramp_tempo(uint8_t g, uint32_t tns, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_tempo) s->cb_tempo(g, tns);
}
void tramp_time_sig(uint8_t g, uint8_t num, uint8_t denom, uint8_t /*n32*/, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_time_sig) s->cb_time_sig(g, num, denom);
}
void tramp_metronome(uint8_t g, uint8_t pri, uint8_t a1, uint8_t a2, uint8_t a3,
                     uint8_t s1, uint8_t s2, void* ctx) {
    auto* st_ = static_cast<DeviceState*>(ctx);
    if (st_->cb_metronome) st_->cb_metronome(g, pri, a1, a2, a3, s1, s2);
}
void tramp_key_sig(uint8_t g, uint8_t /*addr*/, uint8_t /*ch*/,
                   int8_t sf, uint8_t /*tonic*/, uint8_t key_type, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_key_sig) s->cb_key_sig(g, sf, key_type == 1);
}
void tramp_chord(uint8_t g, uint8_t addr, uint8_t ch,
                 int8_t tsf, uint8_t tnote, uint8_t ctype,
                 uint8_t a1t, uint8_t a1d, uint8_t a2t, uint8_t a2d,
                 uint8_t a3t, uint8_t a3d, uint8_t a4t, uint8_t a4d,
                 int8_t bsf, uint8_t bn, uint8_t bt,
                 uint8_t b1t, uint8_t b1d, uint8_t b2t, uint8_t b2d,
                 void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (!s->cb_chord) return;
    ChordDescriptor c{};
    c.address = addr;       c.channel = ch;
    c.tonicSharpFlat = tsf; c.tonicNote = tnote; c.chordType = ctype;
    c.alt1Type = a1t; c.alt1Degree = a1d;
    c.alt2Type = a2t; c.alt2Degree = a2d;
    c.alt3Type = a3t; c.alt3Degree = a3d;
    c.alt4Type = a4t; c.alt4Degree = a4d;
    c.bassSharpFlat = bsf;  c.bassNote = bn;     c.bassChordType = bt;
    c.bassAlt1Type = b1t; c.bassAlt1Degree = b1d;
    c.bassAlt2Type = b2t; c.bassAlt2Degree = b2d;
    s->cb_chord(g, c);
}
void tramp_flex_text(uint8_t g, uint8_t /*format*/, uint8_t /*addr*/, uint8_t /*ch*/,
                     uint8_t bank, uint8_t status,
                     const uint8_t* text, uint8_t len, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_flex_text) s->cb_flex_text(g, bank, status,
                                          reinterpret_cast<const char*>(text), len);
}

// MT 0xF UMP Stream
void tramp_endpoint_discovery(uint8_t /*umj*/, uint8_t /*umi*/, uint8_t filter, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_endpoint_discovery) s->cb_endpoint_discovery(filter);
}
void tramp_endpoint_info(uint8_t umj, uint8_t umi, bool sfb, uint8_t nfb,
                         bool m2, bool m1, bool rxj, bool txj, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_endpoint_info) s->cb_endpoint_info(umj, umi, sfb, nfb, m2, m1, rxj, txj);
}
void tramp_device_identity(uint32_t mfr, uint16_t fam, uint16_t mod, uint32_t ver, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (!s->cb_device_identity) return;
    uint8_t mfrId[3] = {
        (uint8_t)((mfr >> 16) & 0xFF),
        (uint8_t)((mfr >>  8) & 0xFF),
        (uint8_t)( mfr        & 0xFF),
    };
    s->cb_device_identity(mfrId, fam, mod, ver);
}
void tramp_stream_text(uint16_t status, uint8_t format,
                       const uint8_t* data, uint8_t len, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_stream_text) s->cb_stream_text(status, format,
                                              reinterpret_cast<const char*>(data), len);
}
void tramp_fb_name(uint8_t format, uint8_t fb_num,
                   const uint8_t* name, uint8_t len, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_fb_name) s->cb_fb_name(fb_num, format,
                                      reinterpret_cast<const char*>(name), len);
}
void tramp_config_request(uint8_t protocol, bool /*rxj*/, bool /*txj*/, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_stream_config_request) s->cb_stream_config_request(protocol);
}
void tramp_config_notify(uint8_t protocol, bool /*rxj*/, bool /*txj*/, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_stream_config_notify) s->cb_stream_config_notify(protocol);
}
void tramp_fb_discovery(uint8_t fb_num, uint8_t filter, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    if (s->cb_fb_discovery) s->cb_fb_discovery(fb_num, filter);
}
void tramp_fb_info(bool active, uint8_t fb_num,
                   uint8_t direction, uint8_t ui_hint,
                   uint8_t fg, uint8_t ng, uint8_t civ,
                   uint8_t max_sx8, uint8_t protocol, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    // The C99 callback delivers max_sysex8_streams (uint8_t count); the
    // wrapper currently exposes a coarser bool (does this FB support SysEx8?).
    // Forward boolean presence so users at least know the capability exists.
    if (s->cb_fb_info) s->cb_fb_info(active, fb_num, direction, ui_hint,
                                     fg, ng, civ, /*sysex8*/ max_sx8 != 0, protocol);
}
void tramp_clip(bool start, void* ctx) {
    auto* s = static_cast<DeviceState*>(ctx);
    // Spec ClipCb is (group, status). UMP Stream Clip is endpoint-wide so
    // group is 0; status follows the M2-104 §7.1 codes (0x20 start, 0x21 end).
    if (s->cb_clip) s->cb_clip(0, start ? 0x20 : 0x21);
}

// ----- midi2_proc reassembled SysEx -> std::function -----
// proc.context == &dispatch (so midi2_dispatch_feed works for on_ump). To
// reach DeviceState from the SysEx trampolines we hop through
// dispatch.context, which is set to DeviceState* in begin().
void tramp_proc_sysex7(uint8_t group, const uint8_t* data, uint16_t len, void* ctx) {
    auto* dp = static_cast<midi2_dispatch*>(ctx);
    auto* s  = static_cast<DeviceState*>(dp->context);
    if (s->cb_sysex7)    s->cb_sysex7(group, data, len);
    if (s->cb_sysex7_ci) s->cb_sysex7_ci(group, data, len);
}
void tramp_proc_sysex8(uint8_t group, uint8_t stream_id,
                       const uint8_t* data, uint16_t len, void* ctx) {
    auto* dp = static_cast<midi2_dispatch*>(ctx);
    auto* s  = static_cast<DeviceState*>(dp->context);
    if (s->cb_sysex8) s->cb_sysex8(group, stream_id, data, len);
}

}  // anonymous namespace

Device::Device() {
    _state = new DeviceState{};
}

Device::~Device() {
    delete st(_state);
}

void Device::begin() {
    auto* s = st(_state);
    midi2_proc_init(&s->proc,
                    s->sysex7_buf, sizeof(s->sysex7_buf),
                    s->sysex8_buf, sizeof(s->sysex8_buf));
    midi2_dispatch_init(&s->dispatch);

    // Wire dispatch callbacks. context is shared (DeviceState*) so every
    // trampoline can reach the std::function slots installed via Device::onXxx.
    s->dispatch.context = s;
    s->dispatch.on_noop                 = tramp_noop;
    s->dispatch.on_jr_clock             = tramp_jr_clock;
    s->dispatch.on_jr_timestamp         = tramp_jr_timestamp;
    s->dispatch.on_dctpq                = tramp_dctpq;
    s->dispatch.on_dc                   = tramp_dc;
    s->dispatch.on_system               = tramp_system;
    s->dispatch.on_cv1_note_on          = tramp_cv1_note_on;
    s->dispatch.on_cv1_note_off         = tramp_cv1_note_off;
    s->dispatch.on_cv1_poly_pressure    = tramp_cv1_poly_pressure;
    s->dispatch.on_cv1_cc               = tramp_cv1_cc;
    s->dispatch.on_cv1_program          = tramp_cv1_program;
    s->dispatch.on_cv1_chan_pressure    = tramp_cv1_chan_pressure;
    s->dispatch.on_cv1_pitch_bend       = tramp_cv1_pitch_bend;
    // dispatch.on_sysex7 is intentionally left null — see comment near the
    // SysEx7 trampolines. Reassembly goes via proc.on_sysex7 below.
    s->dispatch.on_note_on              = tramp_note_on;
    s->dispatch.on_note_off             = tramp_note_off;
    s->dispatch.on_poly_pressure        = tramp_poly_pressure;
    s->dispatch.on_cc                   = tramp_cc;
    s->dispatch.on_program              = tramp_program;
    s->dispatch.on_chan_pressure        = tramp_chan_pressure;
    s->dispatch.on_pitch_bend           = tramp_pitch_bend;
    s->dispatch.on_per_note_pb          = tramp_per_note_pb;
    s->dispatch.on_reg_per_note         = tramp_reg_per_note;
    s->dispatch.on_asn_per_note         = tramp_asn_per_note;
    s->dispatch.on_rpn                  = tramp_rpn;
    s->dispatch.on_nrpn                 = tramp_nrpn;
    s->dispatch.on_rel_rpn              = tramp_rel_rpn;
    s->dispatch.on_rel_nrpn             = tramp_rel_nrpn;
    s->dispatch.on_per_note_mgmt        = tramp_per_note_mgmt;
    s->dispatch.on_tempo                = tramp_tempo;
    s->dispatch.on_time_sig             = tramp_time_sig;
    s->dispatch.on_metronome            = tramp_metronome;
    s->dispatch.on_key_sig              = tramp_key_sig;
    s->dispatch.on_chord                = tramp_chord;
    s->dispatch.on_flex_text            = tramp_flex_text;
    s->dispatch.on_endpoint_discovery   = tramp_endpoint_discovery;
    s->dispatch.on_endpoint_info        = tramp_endpoint_info;
    s->dispatch.on_device_identity      = tramp_device_identity;
    s->dispatch.on_stream_text          = tramp_stream_text;
    s->dispatch.on_fb_name              = tramp_fb_name;
    s->dispatch.on_config_request       = tramp_config_request;
    s->dispatch.on_config_notify        = tramp_config_notify;
    s->dispatch.on_fb_discovery         = tramp_fb_discovery;
    s->dispatch.on_fb_info              = tramp_fb_info;
    s->dispatch.on_clip                 = tramp_clip;

    // Wire proc -> dispatch (UMP) + reassembled SysEx callbacks.
    s->proc.context   = &s->dispatch;
    s->proc.on_ump    = midi2_dispatch_feed;
    s->proc.on_sysex7 = tramp_proc_sysex7;
    s->proc.on_sysex8 = tramp_proc_sysex8;

    // mounted/alt_setting are set by the caller via setMounted/setAltSetting
    // when USB enumerates. Default state assumes "not mounted yet".
    s->jr_heartbeat_interval_ms = 0;
    s->jr_last_heartbeat_ms = 0;
    s->jr_timestamp = 0;
    s->jr_heartbeat_initialized = false;
}

void Device::task() {
    auto* s = st(_state);

    // The library does not call any platform USB stack here. The caller's
    // main loop is expected to call its own driver's task (e.g. tud_task on
    // TinyUSB) and to push received UMP into Device::feedRx(). See examples/
    // for the per-platform recipe.

    // JR Timestamp heartbeat. Defensive pattern: when a non-zero interval
    // was set via enableJRHeartbeat, emit a JR Timestamp every
    // interval_ms milliseconds. Keeps Linux ALSA polling alive on hosts
    // that drop idle endpoints. First task() after enable always fires
    // once, regardless of clock — flag avoids re-firing when
    // last_heartbeat_ms happens to be 0. Skipped when no clock is wired.
    if (s->jr_heartbeat_interval_ms > 0 && s->now_fn) {
        uint32_t now = platform_now_ms(s);
        bool fire = !s->jr_heartbeat_initialized
                  || (now - s->jr_last_heartbeat_ms) >= s->jr_heartbeat_interval_ms;
        if (fire) {
            // Use a monotonic counter as the JR Timestamp value. Spec says
            // it should be 1/31250 sec ticks for jitter-reduction
            // receivers, but this heartbeat exists purely to keep the USB
            // endpoint active (observed on Ubuntu 24.04 / kernel 6.8 ALSA
            // dropping idle endpoints). Value is irrelevant for the
            // keep-alive use case; receivers wanting precise JR timing
            // negotiate it via UMP Stream Config and supply their own.
            uint32_t w = midi2_msg_jr_timestamp(++s->jr_timestamp);
            device_write(s, &w, 1);
            s->jr_last_heartbeat_ms = now;
            s->jr_heartbeat_initialized = true;
        }
    }
}

bool Device::isMounted() const     { return st(_state)->mounted; }
uint8_t Device::altSetting() const { return st(_state)->alt_setting; }

// ==================== MT 0x0 Utility senders ====================
// MT 0x0 is Groupless per M2-104-UM v1.1.2 section 1.4. The public
// signatures keep the leading uint8_t group for source compatibility
// with existing recipes; the value is ignored on the wire.

bool Device::sendNoop(uint8_t group) {
    (void)group;
    uint32_t w = midi2_msg_noop();
    return device_write(st(_state), &w, 1);
}

bool Device::sendJRClock(uint8_t group, uint16_t timestamp) {
    (void)group;
    uint32_t w = midi2_msg_jr_clock(timestamp);
    return device_write(st(_state), &w, 1);
}

bool Device::sendJRTimestamp(uint8_t group, uint16_t timestamp) {
    (void)group;
    uint32_t w = midi2_msg_jr_timestamp(timestamp);
    return device_write(st(_state), &w, 1);
}

bool Device::sendDctpq(uint16_t tpq) {
    uint32_t w = midi2_msg_dctpq(tpq);
    return device_write(st(_state), &w, 1);
}

bool Device::sendDeltaClockstamp(uint32_t ticks) {
    uint32_t w = midi2_msg_delta_clockstamp(ticks);
    return device_write(st(_state), &w, 1);
}

void Device::enableJRHeartbeat(uint32_t intervalMs) {
    auto* s = st(_state);
    s->jr_heartbeat_interval_ms = intervalMs;
    s->jr_last_heartbeat_ms = 0;
    s->jr_heartbeat_initialized = false;
}

// ==================== MT 0x1 System senders (v0.3.0 wrappers) ====================

bool Device::sendTuneRequest(uint8_t group) {
    uint32_t w = midi2_msg_system_tune_request(group);
    return device_write(st(_state), &w, 1);
}

bool Device::sendClock(uint8_t group) {
    uint32_t w = midi2_msg_system_timing_clock(group);
    return device_write(st(_state), &w, 1);
}

bool Device::sendStart(uint8_t group) {
    uint32_t w = midi2_msg_system_start(group);
    return device_write(st(_state), &w, 1);
}

bool Device::sendContinue(uint8_t group) {
    uint32_t w = midi2_msg_system_continue(group);
    return device_write(st(_state), &w, 1);
}

bool Device::sendStop(uint8_t group) {
    uint32_t w = midi2_msg_system_stop(group);
    return device_write(st(_state), &w, 1);
}

bool Device::sendActiveSensing(uint8_t group) {
    uint32_t w = midi2_msg_system_active_sensing(group);
    return device_write(st(_state), &w, 1);
}

bool Device::sendSystemReset(uint8_t group) {
    uint32_t w = midi2_msg_system_reset(group);
    return device_write(st(_state), &w, 1);
}

bool Device::sendMTC(uint8_t group, uint8_t timeCode) {
    uint32_t w = midi2_msg_system_mtc(group, timeCode);
    return device_write(st(_state), &w, 1);
}

bool Device::sendSongSelect(uint8_t group, uint8_t songNumber) {
    uint32_t w = midi2_msg_system_song_select(group, songNumber);
    return device_write(st(_state), &w, 1);
}

bool Device::sendSongPosition(uint8_t group, uint16_t beats14) {
    uint32_t w = midi2_msg_system_song_position(group, beats14);
    return device_write(st(_state), &w, 1);
}

bool Device::sendSystemGeneric(uint8_t group, uint8_t status, uint8_t data1, uint8_t data2) {
    // 1-word MT 0x1 with explicit status + data bytes; useful when status has no named wrapper.
    uint32_t w = ((uint32_t)0x1u << 28)
               | ((uint32_t)(group & 0x0F) << 24)
               | ((uint32_t)status << 16)
               | ((uint32_t)(data1 & 0x7F) << 8)
               | (uint32_t)(data2 & 0x7F);
    return device_write(st(_state), &w, 1);
}

// ==================== MT 0x2 MIDI 1.0 ====================
// MIDI 1.0 status bytes (channel-voice): 0x80 NoteOff, 0x90 NoteOn, 0xA0 PolyP,
// 0xB0 CC, 0xC0 ProgramChange, 0xD0 ChanP, 0xE0 PitchBend.

bool Device::sendNoteOn1(uint8_t group, uint8_t channel, uint8_t note, uint8_t velocity) {
    uint32_t w = midi2_msg_from_midi1(group, (uint8_t)(0x90 | (channel & 0x0F)), note, velocity);
    return device_write(st(_state), &w, 1);
}

bool Device::sendNoteOff1(uint8_t group, uint8_t channel, uint8_t note, uint8_t velocity) {
    uint32_t w = midi2_msg_from_midi1(group, (uint8_t)(0x80 | (channel & 0x0F)), note, velocity);
    return device_write(st(_state), &w, 1);
}

bool Device::sendCC1(uint8_t group, uint8_t channel, uint8_t index, uint8_t value) {
    uint32_t w = midi2_msg_from_midi1(group, (uint8_t)(0xB0 | (channel & 0x0F)), index, value);
    return device_write(st(_state), &w, 1);
}

bool Device::sendProgram1(uint8_t group, uint8_t channel, uint8_t program) {
    uint32_t w = midi2_msg_from_midi1(group, (uint8_t)(0xC0 | (channel & 0x0F)), program, 0);
    return device_write(st(_state), &w, 1);
}

bool Device::sendPitchBend1(uint8_t group, uint8_t channel, uint16_t value14) {
    uint8_t lsb = (uint8_t)(value14 & 0x7F);
    uint8_t msb = (uint8_t)((value14 >> 7) & 0x7F);
    uint32_t w = midi2_msg_from_midi1(group, (uint8_t)(0xE0 | (channel & 0x0F)), lsb, msb);
    return device_write(st(_state), &w, 1);
}

bool Device::sendChannelPressure1(uint8_t group, uint8_t channel, uint8_t pressure) {
    uint32_t w = midi2_msg_from_midi1(group, (uint8_t)(0xD0 | (channel & 0x0F)), pressure, 0);
    return device_write(st(_state), &w, 1);
}

bool Device::sendPolyPressure1(uint8_t group, uint8_t channel, uint8_t note, uint8_t pressure) {
    uint32_t w = midi2_msg_from_midi1(group, (uint8_t)(0xA0 | (channel & 0x0F)), note, pressure);
    return device_write(st(_state), &w, 1);
}

// ==================== MT 0x3 SysEx7 ====================
// Fragmenter splits arbitrary-length payload into MT 0x3 packets (up to 6
// payload bytes per UMP). Backpressure on individual packets is silently
// dropped; the wrapper returns true if the call was attempted.
bool Device::sendSysEx7(uint8_t group, const uint8_t* data, uint16_t len) {
    if (len > 0 && !data) return false;
    midi2_proc_send_sysex7(group, data, len, &device_write_trampoline, _state);
    return true;
}

// ==================== MT 0x4 MIDI 2.0 ====================

bool Device::sendNoteOn(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity,
                        uint8_t attrType, uint16_t attrData) {
    uint32_t words[2];
    midi2_msg_note_on(words, group, channel, note, velocity, attrType, attrData);
    return device_write(st(_state), words, 2);
}

bool Device::sendNoteOff(uint8_t group, uint8_t channel, uint8_t note, uint16_t velocity,
                         uint8_t attrType, uint16_t attrData) {
    uint32_t words[2];
    midi2_msg_note_off(words, group, channel, note, velocity, attrType, attrData);
    return device_write(st(_state), words, 2);
}
bool Device::sendPolyPressure(uint8_t group, uint8_t channel, uint8_t note, uint32_t pressure) {
    uint32_t words[2];
    midi2_msg_poly_pressure(words, group, channel, note, pressure);
    return device_write(st(_state), words, 2);
}

bool Device::sendCC(uint8_t group, uint8_t channel, uint8_t index, uint32_t value) {
    uint32_t words[2];
    midi2_msg_cc(words, group, channel, index, value);
    return device_write(st(_state), words, 2);
}

bool Device::sendRpn(uint8_t group, uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t value) {
    uint32_t words[2];
    midi2_msg_rpn(words, group, channel, msb, lsb, value);
    return device_write(st(_state), words, 2);
}

bool Device::sendNrpn(uint8_t group, uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t value) {
    uint32_t words[2];
    midi2_msg_nrpn(words, group, channel, msb, lsb, value);
    return device_write(st(_state), words, 2);
}

bool Device::sendRelRpn(uint8_t group, uint8_t channel, uint8_t msb, uint8_t lsb, int32_t delta) {
    uint32_t words[2];
    midi2_msg_rel_rpn(words, group, channel, msb, lsb, (uint32_t)delta);
    return device_write(st(_state), words, 2);
}

bool Device::sendRelNrpn(uint8_t group, uint8_t channel, uint8_t msb, uint8_t lsb, int32_t delta) {
    uint32_t words[2];
    midi2_msg_rel_nrpn(words, group, channel, msb, lsb, (uint32_t)delta);
    return device_write(st(_state), words, 2);
}

bool Device::sendProgram(uint8_t group, uint8_t channel, uint8_t program,
                         uint8_t bankMSB, uint8_t bankLSB, bool bankValid) {
    uint32_t words[2];
    midi2_msg_program(words, group, channel, program, bankValid, bankMSB, bankLSB);
    return device_write(st(_state), words, 2);
}

bool Device::sendChannelPressure(uint8_t group, uint8_t channel, uint32_t pressure) {
    uint32_t words[2];
    midi2_msg_chan_pressure(words, group, channel, pressure);
    return device_write(st(_state), words, 2);
}

bool Device::sendPitchBend(uint8_t group, uint8_t channel, uint32_t value) {
    uint32_t words[2];
    midi2_msg_pitch_bend(words, group, channel, value);
    return device_write(st(_state), words, 2);
}

bool Device::sendPerNotePitchBend(uint8_t group, uint8_t channel, uint8_t note, uint32_t value) {
    uint32_t words[2];
    midi2_msg_per_note_pb(words, group, channel, note, value);
    return device_write(st(_state), words, 2);
}

bool Device::sendRegPerNoteController(uint8_t group, uint8_t channel, uint8_t note,
                                      uint8_t index, uint32_t value) {
    uint32_t words[2];
    midi2_msg_reg_per_note_ctrl(words, group, channel, note, index, value);
    return device_write(st(_state), words, 2);
}

bool Device::sendAsnPerNoteController(uint8_t group, uint8_t channel, uint8_t note,
                                      uint8_t index, uint32_t value) {
    uint32_t words[2];
    midi2_msg_asn_per_note_ctrl(words, group, channel, note, index, value);
    return device_write(st(_state), words, 2);
}

bool Device::sendPerNoteManagement(uint8_t group, uint8_t channel, uint8_t note,
                                   bool detach, bool reset) {
    uint32_t words[2];
    midi2_msg_per_note_mgmt(words, group, channel, note, detach, reset);
    return device_write(st(_state), words, 2);
}

// ==================== Convenience senders (Arduino-style) ====================
// Thin shims over the verbose senders. Group defaults to 0; MT 0x4
// attribute fields default to 0. Zero runtime overhead — the compiler
// inlines these into a direct call to the verbose form.
bool Device::noteOn(uint8_t channel, uint8_t note, uint16_t velocity) {
    return sendNoteOn(/*group*/ 0, channel, note, velocity);
}
bool Device::noteOff(uint8_t channel, uint8_t note, uint16_t velocity) {
    return sendNoteOff(/*group*/ 0, channel, note, velocity);
}
bool Device::cc(uint8_t channel, uint8_t index, uint32_t value) {
    return sendCC(/*group*/ 0, channel, index, value);
}
bool Device::pitchBend(uint8_t channel, uint32_t value) {
    return sendPitchBend(/*group*/ 0, channel, value);
}

// ==================== MT 0x5 SysEx8 ====================
// Fragmenter splits payload into 4-word MT 0x5 packets (13 payload bytes
// each, stream_id in word 0). v0.3.0+ feature.
bool Device::sendSysEx8(uint8_t group, uint8_t streamId, const uint8_t* data, uint16_t len) {
    if (len > 0 && !data) return false;
    midi2_proc_send_sysex8(group, streamId, data, len, &device_write_trampoline, _state);
    return true;
}

// ==================== MT 0xD Flex Data (4 words each) ====================

bool Device::sendTempo(uint8_t group, uint32_t tenNsPerQuarter) {
    uint32_t words[4];
    midi2_msg_tempo(words, group, tenNsPerQuarter);
    return device_write(st(_state), words, 4);
}

bool Device::sendTimeSignature(uint8_t group, uint8_t numerator, uint8_t denominator,
                                uint8_t num32ndNotes) {
    uint32_t words[4];
    midi2_msg_time_sig(words, group, numerator, denominator, num32ndNotes);
    return device_write(st(_state), words, 4);
}

bool Device::sendMetronome(uint8_t group, uint8_t primary, uint8_t acc1, uint8_t acc2,
                           uint8_t acc3, uint8_t sub1, uint8_t sub2) {
    uint32_t words[4];
    midi2_msg_metronome(words, group, primary, acc1, acc2, acc3, sub1, sub2);
    return device_write(st(_state), words, 4);
}

bool Device::sendKeySignature(uint8_t group, int8_t sharpsFlats, bool minor) {
    uint32_t words[4];
    midi2_msg_key_sig(words, group, sharpsFlats, minor);
    return device_write(st(_state), words, 4);
}

bool Device::sendChordName(uint8_t group, const ChordDescriptor& c) {
    uint32_t words[4];
    midi2_msg_chord_name(words, group, c.address, c.channel,
                         c.tonicSharpFlat, c.tonicNote, c.chordType,
                         c.alt1Type, c.alt1Degree,
                         c.alt2Type, c.alt2Degree,
                         c.alt3Type, c.alt3Degree,
                         c.alt4Type, c.alt4Degree,
                         c.bassSharpFlat, c.bassNote, c.bassChordType,
                         c.bassAlt1Type, c.bassAlt1Degree,
                         c.bassAlt2Type, c.bassAlt2Degree);
    return device_write(st(_state), words, 4);
}

bool Device::sendFlexText(uint8_t group, uint8_t statusBank, uint8_t status, const char* text) {
    if (!text) return false;
    // Single-UMP path only (format=0 complete). Strings > 12 bytes need
    // multi-UMP fragmentation (formats 1/2/3) which is not yet implemented;
    // refuse rather than truncate silently. Fragmenter target v0.1.x.
    size_t total = 0;
    while (text[total] != '\0') {
        if (++total > 12) return false;
    }
    uint32_t words[4];
    midi2_msg_flex_text(words, group, /*format*/ 0, /*address*/ 0, /*channel*/ 0,
                        statusBank, status,
                        reinterpret_cast<const uint8_t*>(text), (uint8_t)total);
    return device_write(st(_state), words, 4);
}

// ==================== MT 0xF UMP Stream ====================
// Endpoint Info Notification (status 0x01). 4 words per M2-104 §7.1.5.
bool Device::sendEndpointInfo(uint8_t umpVerMajor, uint8_t umpVerMinor,
                              bool staticFb, uint8_t numFb,
                              bool midi2Proto, bool midi1Proto,
                              bool rxJr, bool txJr) {
    uint32_t words[4] = {0};
    midi2_msg_stream_endpoint_info(words, umpVerMajor, umpVerMinor,
                                    staticFb, numFb,
                                    midi2Proto, midi1Proto,
                                    rxJr, txJr);
    return device_write(st(_state), words, 4);
}

// Stream Configuration Notification (status 0x06). 4 words.
//
// JR_TX_ENABLE is derived from the device's current JR Heartbeat state
// (set by enableJRHeartbeat / disableJRHeartbeat); JR_RX_ENABLE stays
// false by default because the wrapper does not currently process
// inbound JR Timestamps. Callers that need explicit control over both
// bits can construct + emit the UMP via the midi2 C99 layer directly.
bool Device::sendStreamConfigNotify(uint8_t protocol) {
    auto* s = st(_state);
    const bool tx_jr_enable = s->jr_heartbeat_interval_ms > 0;
    const bool rx_jr_enable = false;
    uint32_t words[4] = {0};
    midi2_msg_stream_config_notify(words, protocol, rx_jr_enable, tx_jr_enable);
    return device_write(s, words, 4);
}

// Function Block Info Notification (status 0x11). 4 words per M2-104 §7.1.13.
// The wrapper keeps `bool sysex8` for source compatibility; pass `1` for
// "supports SysEx8" / `0` for "not supported" downstream to midi2 v0.4.0's
// uint8_t max_sysex8_streams field.
bool Device::sendFbInfo(bool active, uint8_t fbNum,
                        uint8_t direction, uint8_t uiHint,
                        uint8_t firstGroup, uint8_t numGroups,
                        uint8_t midiCiVer, bool sysex8, uint8_t protocol) {
    uint32_t words[4] = {0};
    const uint8_t max_sx8 = sysex8 ? 1 : 0;
    midi2_msg_stream_fb_info(words, active, fbNum, direction, uiHint,
                              firstGroup, numGroups,
                              midiCiVer, max_sx8, protocol);
    return device_write(st(_state), words, 4);
}

// ManufacturerId is uint8_t[3] MSB-first; pack to lower 24 bits of uint32_t.
bool Device::sendDeviceIdentity(const uint8_t mfrId[3], uint16_t family,
                                uint16_t model, uint32_t version) {
    if (!mfrId) return false;
    uint32_t packed_mfr = ((uint32_t)mfrId[0] << 16)
                        | ((uint32_t)mfrId[1] << 8)
                        |  (uint32_t)mfrId[2];
    midi2_proc_send_device_identity(packed_mfr, family, model, version,
                                     &device_write_trampoline, _state);
    return true;
}

bool Device::sendEndpointNameUpdate(const char* name) {
    if (!name) return false;
    midi2_proc_send_endpoint_name(name, &device_write_trampoline, _state);
    return true;
}

bool Device::sendProductInstanceIdUpdate(const char* id) {
    if (!id) return false;
    midi2_proc_send_product_id(id, &device_write_trampoline, _state);
    return true;
}

bool Device::sendFbNameUpdate(uint8_t fbIdx, const char* name) {
    if (!name) return false;
    midi2_proc_send_fb_name(fbIdx, name, &device_write_trampoline, _state);
    return true;
}

// Start/End of Clip are endpoint-wide UMP Stream messages (M2-104 §7.1.10-11).
// No group field — emitted to the endpoint as a whole. Backed by upstream
// midi2_msg_stream_start_of_clip / midi2_msg_stream_end_of_clip helpers.
bool Device::sendStartOfClip() {
    uint32_t words[4] = {0};
    midi2_msg_stream_start_of_clip(words);
    return device_write(st(_state), words, 4);
}

bool Device::sendEndOfClip() {
    uint32_t words[4] = {0};
    midi2_msg_stream_end_of_clip(words);
    return device_write(st(_state), words, 4);
}

// ==================== Callback setters ====================
// Each setter stores the std::function in DeviceState. The C99 trampolines
// (tramp_*) read those slots when dispatch fires; setting a callback after
// begin() takes effect on the next inbound message.

#define MIDI2CPP_SETTER(method, field, type) \
    void Device::method(type cb) { st(_state)->field = std::move(cb); }

MIDI2CPP_SETTER(onNoop,                 cb_noop,                 NoArgCb)
MIDI2CPP_SETTER(onJRClock,              cb_jr_clock,             UtilityCb)
MIDI2CPP_SETTER(onJRTimestamp,          cb_jr_timestamp,         UtilityCb)
void Device::onDctpq(std::function<void(uint16_t)> cb) {
    st(_state)->cb_dctpq = std::move(cb);
}
void Device::onDeltaClockstamp(std::function<void(uint32_t)> cb) {
    st(_state)->cb_delta_clockstamp = std::move(cb);
}

MIDI2CPP_SETTER(onSystem,               cb_system,               SystemCb)

MIDI2CPP_SETTER(onNoteOn1,              cb_note_on1,             Note1Cb)
MIDI2CPP_SETTER(onNoteOff1,             cb_note_off1,            Note1Cb)
MIDI2CPP_SETTER(onCC1,                  cb_cc1,                  Controller1Cb)
MIDI2CPP_SETTER(onProgram1,             cb_program1,             Program1Cb)
MIDI2CPP_SETTER(onPitchBend1,           cb_pitch_bend1,          PitchBend1Cb)
MIDI2CPP_SETTER(onChannelPressure1,     cb_chan_pressure1,       Pressure1Cb)
MIDI2CPP_SETTER(onPolyPressure1,        cb_poly_pressure1,       PolyPressure1Cb)

void Device::setUpscaleMt2(bool enabled) {
    st(_state)->dispatch.upscale_mt2 = enabled;
}

MIDI2CPP_SETTER(onSysEx7,               cb_sysex7,               SysEx7Cb)

MIDI2CPP_SETTER(onNoteOn,               cb_note_on,              NoteCb)
MIDI2CPP_SETTER(onNoteOff,              cb_note_off,             NoteCb)
MIDI2CPP_SETTER(onPolyPressure,         cb_poly_pressure,        PolyPressureCb32)
MIDI2CPP_SETTER(onCC,                   cb_cc,                   ControllerCb32)
MIDI2CPP_SETTER(onProgram,              cb_program,              ProgramCb)
MIDI2CPP_SETTER(onChannelPressure,      cb_chan_pressure,        PressureCb32)
MIDI2CPP_SETTER(onPitchBend,            cb_pitch_bend,           Pb32Cb)
MIDI2CPP_SETTER(onRpn,                  cb_rpn,                  RpnNrpnCb)
MIDI2CPP_SETTER(onNrpn,                 cb_nrpn,                 RpnNrpnCb)
MIDI2CPP_SETTER(onRelRpn,               cb_rel_rpn,              RelRpnNrpnCb)
MIDI2CPP_SETTER(onRelNrpn,              cb_rel_nrpn,             RelRpnNrpnCb)
MIDI2CPP_SETTER(onPerNotePitchBend,     cb_per_note_pb,          PerNotePbCb)
MIDI2CPP_SETTER(onRegPerNoteController, cb_reg_per_note,         PerNoteCtrlCb)
MIDI2CPP_SETTER(onAsnPerNoteController, cb_asn_per_note,         PerNoteCtrlCb)

// Arduino-style callback overloads. Each one wraps the simple lambda into
// a verbose-shaped one that fits the existing storage slot, so the
// dispatch trampolines stay untouched and only one callback path runs at
// a time. Setting the simple form overwrites a previously installed
// verbose form (and vice versa) — "the latest setter wins".
void Device::onNoteOn(NoteSimpleCb cb) {
    st(_state)->cb_note_on =
        [cb = std::move(cb)](uint8_t /*g*/, uint8_t ch, uint8_t note, uint16_t vel,
                              uint8_t /*at*/, uint16_t /*ad*/) {
            if (cb) cb(ch, note, vel);
        };
}
void Device::onNoteOff(NoteSimpleCb cb) {
    st(_state)->cb_note_off =
        [cb = std::move(cb)](uint8_t /*g*/, uint8_t ch, uint8_t note, uint16_t vel,
                              uint8_t /*at*/, uint16_t /*ad*/) {
            if (cb) cb(ch, note, vel);
        };
}
void Device::onCC(ControllerSimpleCb cb) {
    st(_state)->cb_cc =
        [cb = std::move(cb)](uint8_t /*g*/, uint8_t ch, uint8_t idx, uint32_t val) {
            if (cb) cb(ch, idx, val);
        };
}
void Device::onPitchBend(PbSimpleCb cb) {
    st(_state)->cb_pitch_bend =
        [cb = std::move(cb)](uint8_t /*g*/, uint8_t ch, uint32_t val) {
            if (cb) cb(ch, val);
        };
}
MIDI2CPP_SETTER(onPerNoteManagement,    cb_per_note_mgmt,        PerNoteMgmtCb)

MIDI2CPP_SETTER(onSysEx8,               cb_sysex8,               SysEx8Cb)

MIDI2CPP_SETTER(onTempo,                cb_tempo,                TempoCb)
MIDI2CPP_SETTER(onTimeSignature,        cb_time_sig,             TimeSigCb)
MIDI2CPP_SETTER(onMetronome,            cb_metronome,            MetronomeCb)
MIDI2CPP_SETTER(onKeySignature,         cb_key_sig,              KeySigCb)
MIDI2CPP_SETTER(onChord,                cb_chord,                ChordCb)
MIDI2CPP_SETTER(onFlexText,             cb_flex_text,            FlexTextCb)

MIDI2CPP_SETTER(onEndpointDiscovery,    cb_endpoint_discovery,   EndpointDiscoveryCb)
MIDI2CPP_SETTER(onEndpointInfo,         cb_endpoint_info,        EndpointInfoCb)
MIDI2CPP_SETTER(onDeviceIdentity,       cb_device_identity,      DeviceIdentityCb)
MIDI2CPP_SETTER(onStreamText,           cb_stream_text,          StreamTextCb)
MIDI2CPP_SETTER(onFbName,               cb_fb_name,              FbNameCb)
MIDI2CPP_SETTER(onStreamConfigRequest,  cb_stream_config_request, StreamConfigCb)
MIDI2CPP_SETTER(onStreamConfigNotify,   cb_stream_config_notify, StreamConfigCb)
MIDI2CPP_SETTER(onFbDiscovery,          cb_fb_discovery,         FbDiscoveryCb)
MIDI2CPP_SETTER(onFbInfo,               cb_fb_info,              FbInfoCb)
MIDI2CPP_SETTER(onClip,                 cb_clip,                 ClipCb)

#undef MIDI2CPP_SETTER

void Device::setGroupRemap(const uint8_t map[16]) {
    if (!map) return;
    auto* s = st(_state);
    for (int i = 0; i < 16; ++i) {
        s->proc.group_map[i] = (uint8_t)(map[i] & 0x0F);
    }
}

// ==================== Friend-access bridge for class CI ====================
void Device::_setCiSysExHook(SysEx7Cb hook) {
    st(_state)->cb_sysex7_ci = std::move(hook);
}

void Device::_ciWriteFnContext(CiWriteFn* outFn, void** outCtx) {
    *outFn  = &device_write_trampoline;
    *outCtx = _state;  // DeviceState*; trampoline forwards to USB transport
}

// ==================== Static helpers (field-tested) ====================

// Downgrade a stream of MT 0x4 (MIDI 2.0 CV) UMPs into MT 0x2 (MIDI 1.0 CV).
// Walks `in` two words at a time (MT 0x4 size) and emits one MT 0x2 word per
// translatable input. Per-Note, RPN/NRPN, and Relative variants are dropped
// silently (no MIDI 1.0 form); caller can detect drops via *outCount < count.
bool Device::downgradeMt4ToMt2(const uint32_t* in, uint8_t count,
                               uint32_t* out, uint8_t* outCount) {
    if (!in || !out || !outCount) return false;
    uint8_t produced = 0;
    for (uint8_t i = 0; i + 2 <= count; i += 2) {
        uint32_t mt4_pair[2] = { in[i], in[i + 1] };
        uint32_t emitted = midi2_msg_mt4_to_mt2(mt4_pair, &out[produced]);
        if (emitted == 1) ++produced;
    }
    *outCount = produced;
    return produced > 0;
}

bool Device::cableEventToUmp(uint32_t cableEvent, uint8_t group, uint32_t* umpOut) {
    if (!umpOut) return false;
    return midi2_msg_cable_event_to_ump(cableEvent, group, umpOut);
}

void Device::setUmpGroup(uint32_t* word0, uint8_t group) {
    if (!word0) return;
    midi2_msg_set_group(word0, group);
}

// ==================== ByteStreamConverter ====================
// Pimpl wrapping midi2_conv_state. Each feed(byte) advances the running
// state; on a complete MIDI 1.0 message (or SysEx fragment) we capture the
// emitted UMP into the ump_cb.

struct BscState {
    midi2_conv_state conv;
    ByteStreamConverter::UmpCb ump_cb;
};

static inline BscState* bsc(void* p) {
    return static_cast<BscState*>(p);
}

ByteStreamConverter::ByteStreamConverter(uint8_t group) {
    auto* s = new BscState{};
    midi2_conv_init(&s->conv, group);
    _state = s;
}

ByteStreamConverter::~ByteStreamConverter() {
    delete bsc(_state);
}

bool ByteStreamConverter::feed(uint8_t byte) {
    auto* s = bsc(_state);
    bool ready = midi2_conv_feed(&s->conv, byte);
    if (ready) {
        if (s->ump_cb) s->ump_cb(s->conv.ump, s->conv.ump_words);
        return true;
    }
    return false;
}

void ByteStreamConverter::onUmp(UmpCb cb) {
    bsc(_state)->ump_cb = std::move(cb);
}

void ByteStreamConverter::reset() {
    auto* s = bsc(_state);
    uint8_t g = s->conv.group;
    midi2_conv_init(&s->conv, g);
}

void ByteStreamConverter::setGroup(uint8_t g) {
    bsc(_state)->conv.group = g;
}

}  // namespace midi2

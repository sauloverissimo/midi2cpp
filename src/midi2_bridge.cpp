// midi2_bridge.cpp, USB MIDI 2.0 multi-slot bridge implementation.
//
// Composes Device + CI + Host with a slot table and the multi-FB
// Stream Discovery responder logic that the esp32-p4-devkit-bridge-midi2
// recipe used to carry inline. The platform glue around this class is
// reduced to a few hundred lines (PHY init, TinyUSB tasks, write
// callbacks); everything else lives here so each new bridge recipe gets
// the same forwarding behaviour for free.

#include "midi2_bridge.h"

#include <cstdio>
#include <cstring>

namespace midi2 {

// ---------------------------------------------------------------------
// Internal tables.
// ---------------------------------------------------------------------

// UMP word count per Message Type (top nibble of word 0). Indexed by MT.
static const uint8_t kMtWordCount[16] = {
    1, 1, 1, 2,   // 0,1,2,3
    2, 4, 1, 1,   // 4,5,6,7
    2, 2, 2, 3,   // 8,9,A,B
    3, 4, 4, 4    // C,D,E,F
};

// USB-MIDI 1.0 packet (4 bytes) -> count of MIDI bytes to feed the
// converter. Index by CIN (low nibble of byte 0). Reserved CINs 0/1
// yield 0 (skip).
static const uint8_t kCinByteCount[16] = {
    0, 0, 2, 3,   // 0,1,2,3
    3, 1, 2, 3,   // 4,5,6,7
    3, 3, 3, 3,   // 8,9,A,B
    2, 2, 3, 1    // C,D,E,F
};

// ---------------------------------------------------------------------
// Pimpl state.
// ---------------------------------------------------------------------

struct Slot {
    bool    active = false;
    uint8_t alt    = 0;
    char    name[64] = {0};
};

struct BridgeState {
    // Composition. Device must be constructed before CI (CI takes a
    // Device&); Bridge owns the lifetime of all three.
    Device device;
    CI     ci{device};
    Host   host;

    // Topology (set before begin, applied at begin).
    uint8_t numSlots       = Bridge::MAX_SLOTS;
    uint8_t groupsPerSlot  = 4;

    // Identity. Defaults match the project's MIDI Association educational
    // prefix; recipes overwrite via Bridge::setManufacturerId etc.
    uint8_t  manufacturerId[3]      = {0x7D, 0x00, 0x00};
    uint16_t family                 = 0x0001;
    uint16_t model                  = 0x0001;
    uint32_t version                = 0x00010000;
    char     endpointName[64]       = "midi2cpp Bridge";
    char     productInstanceId[64]  = "midi2cpp-bridge-0001";

    // Platform write hooks. The Bridge wraps these into Device::WriteFn
    // and Host::WriteFn during begin() so the inner classes do not need
    // to know they live inside a Bridge.
    Bridge::DownstreamWriteFn downstream_write;
    Bridge::UpstreamWriteFn   upstream_write;
    Bridge::NowFn             now;
    Bridge::RngFn             rng;

    // Slot table + per-slot byte stream converters.
    Slot                  slots[Bridge::MAX_SLOTS];
    ByteStreamConverter*  byteConv[Bridge::MAX_SLOTS] = {};

    // Reverse map for MIDI 1.0 host idx -> bridge slot, so an upstream
    // legacy device that arrives via tuh_midi_*_cb gets a slot from the
    // top of the table without colliding with MIDI 2.0 mounts that take
    // low-numbered slots via host().notifyDeviceMounted().
    int8_t midi1SlotMap[Bridge::MAX_SLOTS];

    bool begun = false;
};

// Static_cast helper for the opaque pimpl pointer.
static inline BridgeState* st(void* p) {
    return static_cast<BridgeState*>(p);
}

// ---------------------------------------------------------------------
// Forward UMPs from upstream slot idx to PC, rewriting the group
// nibble into the slot's window.
//
// MT 0x0 (utility / JR), 0xE (reserved) and 0xF (stream) are skipped:
// the bridge owns its own JR heartbeat decision and Stream Discovery
// surface; reserved MTs are dropped to keep the wire clean.
// ---------------------------------------------------------------------
static void forward_ump_to_pc(BridgeState* s, uint8_t idx,
                              const uint32_t* words, size_t count) {
    if (idx >= s->numSlots) return;
    if (!s->downstream_write) return;
    const uint8_t base = (uint8_t)(idx * s->groupsPerSlot);

    size_t i = 0;
    while (i < count) {
        uint8_t mt = (uint8_t)((words[i] >> 28) & 0x0F);
        uint8_t wcount = kMtWordCount[mt];
        if (i + wcount > count) break;

        if (mt != 0x0 && mt != 0xE && mt != 0xF) {
            uint32_t out[4];
            uint8_t in_group = (uint8_t)((words[i] >> 24) & 0x0F);
            uint8_t out_group = (uint8_t)(base + (in_group % s->groupsPerSlot));
            out[0] = (words[i] & 0xF0FFFFFFu)
                   | ((uint32_t)(out_group & 0x0F) << 24);
            for (uint8_t w = 1; w < wcount; ++w) out[w] = words[i + w];
            (void)s->downstream_write(out, wcount);
        }
        i += wcount;
    }
}

// ---------------------------------------------------------------------
// Stream Discovery responses.
// ---------------------------------------------------------------------

static void push_fb_info(BridgeState* s, uint8_t idx) {
    if (idx >= s->numSlots) return;
    const uint8_t base = (uint8_t)(idx * s->groupsPerSlot);
    s->device.sendFbInfo(/*active*/      s->slots[idx].active,
                         /*fb_num*/      idx,
                         /*direction*/   0x03,
                         /*first_group*/ base,
                         /*num_groups*/  s->groupsPerSlot,
                         /*midi_ci_ver*/ 0x02,
                         /*sysex8*/      false,
                         /*protocol*/    0x02);
}

static void push_fb_name(BridgeState* s, uint8_t idx) {
    if (idx >= s->numSlots) return;
    const char* name = (s->slots[idx].active && s->slots[idx].name[0])
                         ? s->slots[idx].name
                         : "(empty slot)";
    s->device.sendFbNameUpdate(idx, name);
}

static void install_stream_responder(BridgeState* s) {
    s->device.onEndpointDiscovery([s](uint8_t filter) {
        if (filter & 0x01) {
            s->device.sendEndpointInfo(/*ump_ver_major*/ 1,
                                       /*ump_ver_minor*/ 1,
                                       /*static_fb*/    false,
                                       /*num_fb*/       s->numSlots,
                                       /*midi2*/        true,
                                       /*midi1*/        true,
                                       /*rx_jr*/        false,
                                       /*tx_jr*/        true);
        }
        if (filter & 0x02) {
            s->device.sendDeviceIdentity(s->manufacturerId,
                                         s->family, s->model, s->version);
        }
        if (filter & 0x04) s->device.sendEndpointNameUpdate(s->endpointName);
        if (filter & 0x08) s->device.sendProductInstanceIdUpdate(s->productInstanceId);
        if (filter & 0x10) s->device.sendStreamConfigNotify(/*protocol*/ 0x02);
    });

    s->device.onFbDiscovery([s](uint8_t fbNum, uint8_t filter) {
        if (fbNum == 0xFF) {
            for (uint8_t i = 0; i < s->numSlots; ++i) {
                if (filter & 0x01) push_fb_info(s, i);
                if (filter & 0x02) push_fb_name(s, i);
            }
        } else if (fbNum < s->numSlots) {
            if (filter & 0x01) push_fb_info(s, fbNum);
            if (filter & 0x02) push_fb_name(s, fbNum);
        }
    });

    s->device.onStreamConfigRequest([s](uint8_t protocol) {
        s->device.sendStreamConfigNotify(protocol);
    });
}

static void install_host_callbacks(BridgeState* s) {
    s->host.onDeviceConnected([s](uint8_t idx, const Host::DeviceIdentity& id) {
        if (idx < s->numSlots) {
            s->slots[idx].active = true;
            s->slots[idx].alt    = id.altSettingActive;
            push_fb_info(s, idx);
            push_fb_name(s, idx);
        }
    });
    s->host.onDeviceDisconnected([s](uint8_t idx) {
        if (idx < s->numSlots) {
            s->slots[idx].active = false;
            s->slots[idx].name[0] = '\0';
            if (s->byteConv[idx]) s->byteConv[idx]->reset();
            push_fb_info(s, idx);
            push_fb_name(s, idx);
        }
    });
    s->host.onIdentityUpdated([s](uint8_t idx, const Host::DeviceIdentity& id) {
        if (idx >= s->numSlots) return;
        if (!id.endpointName[0]) return;
        // snprintf instead of strncpy avoids -Werror=stringop-truncation
        // when the source happens to be exactly cap-1 bytes long.
        std::snprintf(s->slots[idx].name, sizeof(s->slots[idx].name),
                      "%s", id.endpointName);
        push_fb_name(s, idx);
    });
}

// ---------------------------------------------------------------------
// Class members.
// ---------------------------------------------------------------------

Bridge::Bridge() {
    auto* s = new BridgeState{};
    for (auto& m : s->midi1SlotMap) m = -1;
    _state = s;
}

Bridge::~Bridge() {
    auto* s = st(_state);
    for (uint8_t i = 0; i < MAX_SLOTS; ++i) {
        delete s->byteConv[i];
        s->byteConv[i] = nullptr;
    }
    delete s;
    _state = nullptr;
}

void Bridge::setNumSlots(uint8_t n) {
    auto* s = st(_state);
    if (s->begun || n == 0 || n > MAX_SLOTS) return;
    s->numSlots = n;
}

void Bridge::setGroupsPerSlot(uint8_t n) {
    auto* s = st(_state);
    if (s->begun || n == 0 || n > 16) return;
    s->groupsPerSlot = n;
}

uint8_t Bridge::numSlots() const      { return st(_state)->numSlots; }
uint8_t Bridge::groupsPerSlot() const { return st(_state)->groupsPerSlot; }

Device& Bridge::device() { return st(_state)->device; }
CI&     Bridge::ci()     { return st(_state)->ci; }
Host&   Bridge::host()   { return st(_state)->host; }

void Bridge::setManufacturerId(const uint8_t mfrId[3]) {
    if (!mfrId) return;
    auto* s = st(_state);
    s->manufacturerId[0] = mfrId[0];
    s->manufacturerId[1] = mfrId[1];
    s->manufacturerId[2] = mfrId[2];
}

void Bridge::setFamily(uint16_t f)       { st(_state)->family = f; }
void Bridge::setModel(uint16_t m)        { st(_state)->model  = m; }
void Bridge::setVersion(uint32_t v)      { st(_state)->version = v; }

void Bridge::setEndpointName(const char* name) {
    if (!name) return;
    auto* s = st(_state);
    std::snprintf(s->endpointName, sizeof(s->endpointName), "%s", name);
}

void Bridge::setProductInstanceId(const char* id) {
    if (!id) return;
    auto* s = st(_state);
    std::snprintf(s->productInstanceId, sizeof(s->productInstanceId), "%s", id);
}

void Bridge::setDownstreamWriteFn(DownstreamWriteFn fn) {
    st(_state)->downstream_write = std::move(fn);
}
void Bridge::setUpstreamWriteFn(UpstreamWriteFn fn) {
    st(_state)->upstream_write = std::move(fn);
}
void Bridge::setNowFn(NowFn fn) { st(_state)->now = std::move(fn); }
void Bridge::setRngFn(RngFn fn) { st(_state)->rng = std::move(fn); }

void Bridge::begin() {
    auto* s = st(_state);
    if (s->begun) return;
    if (!s->downstream_write || !s->upstream_write) return;
    s->begun = true;

    // Wire inner Device + Host + CI to the platform hooks. The lambdas
    // capture s by value (raw pointer) since the Bridge owns the state
    // for its full lifetime.
    auto* state = s;
    s->device.setWriteFn([state](const uint32_t* w, size_t n) {
        if (state->downstream_write) state->downstream_write(w, n);
    });
    s->host.setWriteFn([state](uint8_t idx, const uint32_t* w, size_t n) {
        if (state->upstream_write) state->upstream_write(idx, w, n);
    });
    if (s->now) {
        s->device.setNowFn(s->now);
        s->host.setNowFn(s->now);
    }
    if (s->rng) {
        s->ci.setRngFn(s->rng);
        s->host.setRngFn(s->rng);
    }
    s->device.setMounted(false);
    s->device.setAltSetting(0);

    // Per-slot MIDI 1.0 byte-stream-to-UMP converters. Each one is
    // pinned to its slot's first group; emitted UMPs go straight to
    // the PC via downstream_write.
    for (uint8_t i = 0; i < s->numSlots; ++i) {
        s->byteConv[i] = new ByteStreamConverter((uint8_t)(i * s->groupsPerSlot));
        s->byteConv[i]->onUmp([state](const uint32_t* w, uint8_t cnt) {
            if (state->downstream_write) state->downstream_write(w, cnt);
        });
    }

    // Lifecycle on the inner classes.
    s->device.begin();
    s->ci.begin(s->manufacturerId, s->family, s->model, s->version);
    s->host.begin();

    install_stream_responder(s);
    install_host_callbacks(s);
}

void Bridge::task() {
    auto* s = st(_state);
    s->device.task();
    s->host.task();
}

void Bridge::slotSetActive(uint8_t idx, bool active, uint8_t alt) {
    auto* s = st(_state);
    if (idx >= s->numSlots) return;
    s->slots[idx].active = active;
    s->slots[idx].alt    = alt;
    if (!active) {
        s->slots[idx].name[0] = '\0';
        if (s->byteConv[idx]) s->byteConv[idx]->reset();
    }
    push_fb_info(s, idx);
    push_fb_name(s, idx);
}

void Bridge::feedDeviceRx(const uint32_t* words, size_t count) {
    auto* s = st(_state);
    if (!words || count == 0) return;
    // m2 Device's feedRx -> midi2_proc_feed processes one UMP packet
    // per call (it ignores word_count and uses MT to size the packet),
    // so iterate packet-by-packet here.
    size_t i = 0;
    while (i < count) {
        uint8_t mt = (uint8_t)((words[i] >> 28) & 0x0F);
        uint8_t wc = kMtWordCount[mt];
        if (i + wc > count) break;
        s->device.feedRx(&words[i], wc);
        i += wc;
    }
}

void Bridge::feedHostRx(uint8_t idx, const uint32_t* words, size_t count) {
    auto* s = st(_state);
    if (!words || count == 0) return;
    if (idx >= s->numSlots) return;

    // Forward (raw, with group rewrite into the slot's window) BEFORE
    // feeding m2 Host: the forward path is fast and host.feedRx may
    // synthesize replies (e.g. CI Discovery follow-ups) that should not
    // race with the inbound burst.
    forward_ump_to_pc(s, idx, words, count);

    // Then feed m2 Host one packet at a time so the parser sees every
    // Stream / Identity / CI message instead of just the first.
    size_t i = 0;
    while (i < count) {
        uint8_t mt = (uint8_t)((words[i] >> 28) & 0x0F);
        uint8_t wc = kMtWordCount[mt];
        if (i + wc > count) break;
        s->host.feedRx(idx, &words[i], wc);
        i += wc;
    }
}

void Bridge::feedHostMidi1Bytes(uint8_t idx, const uint8_t* bytes, size_t count) {
    auto* s = st(_state);
    if (!bytes || count == 0) return;
    if (idx >= s->numSlots) return;
    auto* conv = s->byteConv[idx];
    if (!conv) return;

    // Bytes arrive as USB-MIDI 1.0 packets (4-byte CIN-encoded). Decode
    // CIN to get the count of MIDI bytes per packet.
    size_t off = 0;
    while (off + 4 <= count) {
        uint8_t cin = bytes[off] & 0x0F;
        uint8_t bcount = kCinByteCount[cin];
        for (uint8_t b = 0; b < bcount; ++b) {
            (void)conv->feed(bytes[off + 1 + b]);
        }
        off += 4;
    }
}

void Bridge::setDeviceMounted(bool mounted) {
    st(_state)->device.setMounted(mounted);
}

void Bridge::setDeviceAltSetting(uint8_t alt) {
    st(_state)->device.setAltSetting(alt);
}

}  // namespace midi2

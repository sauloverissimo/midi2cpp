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

    // Identity-bound slot placement. hostIdxToSlot / slotToHostIdx carry
    // the live host idx <-> slot indirection; binding[] holds the
    // persistent identity key per slot; ephemeral[] marks slots placed
    // without an identity (never persisted).
    char     binding[Bridge::MAX_SLOTS][64] = {};
    bool     ephemeral[Bridge::MAX_SLOTS]   = {};
    int8_t   hostIdxToSlot[MIDI2CPP_HOST_MAX_DEVICES];
    int8_t   slotToHostIdx[Bridge::MAX_SLOTS];
    uint32_t mountedAtMs[MIDI2CPP_HOST_MAX_DEVICES] = {};
    Bridge::SlotBindingChangedFn binding_changed;
    Bridge::TrafficFn            traffic_tap;

    // Settle window for identity-less devices (0 = place at mount).
    uint32_t placeTimeoutMs = 3000;

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
    if (idx >= MIDI2CPP_HOST_MAX_DEVICES) return;
    if (!s->downstream_write) return;
    int8_t slot = s->hostIdxToSlot[idx];
    if (slot < 0) return;   // not placed yet: nothing to forward into
    const uint8_t base = (uint8_t)((uint8_t)slot * s->groupsPerSlot);

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
            if (s->traffic_tap) s->traffic_tap(true, out, wcount);
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
    // ui_hint=0x03 (Sender+Receiver): bridge slots are always bidirectional -
    // the upstream device on the host port can both produce and consume UMP
    // relative to the downstream device-side endpoint.
    s->device.sendFbInfo(/*active*/      s->slots[idx].active,
                         /*fb_num*/      idx,
                         /*direction*/   0x03,
                         /*ui_hint*/     0x03,
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

// ---------------------------------------------------------------------
// Identity-bound slot placement.
// ---------------------------------------------------------------------

// Only a COMPLETE text can be a binding key: multi-packet names fire
// identity updates per fragment, and a key cut mid-stream would change
// from boot to boot.
static const char* identity_key(const Host::DeviceIdentity& id) {
    if (id.productInstanceId[0] && id.productInstanceIdComplete)
        return id.productInstanceId;
    if (id.endpointName[0] && id.endpointNameComplete)
        return id.endpointName;
    return nullptr;
}

static void occupy_slot(BridgeState* s, uint8_t slot, uint8_t idx) {
    const auto& id = s->host.identity(idx);
    s->hostIdxToSlot[idx]  = (int8_t)slot;
    s->slotToHostIdx[slot] = (int8_t)idx;
    s->slots[slot].active  = true;
    s->slots[slot].alt     = id.altSettingActive;
    if (id.endpointName[0]) {
        std::snprintf(s->slots[slot].name, sizeof(s->slots[slot].name),
                      "%s", id.endpointName);
    }
    push_fb_info(s, slot);
    push_fb_name(s, slot);
}

static void set_binding(BridgeState* s, uint8_t slot, const char* key) {
    // Single owner: a key lives on exactly one slot. Clear (and persist
    // the clear of) any other slot still holding it, or stale entries
    // accumulate and the device's FB drifts across boots.
    for (uint8_t i = 0; i < s->numSlots; ++i) {
        if (i != slot && std::strcmp(s->binding[i], key) == 0) {
            s->binding[i][0] = '\0';
            if (s->binding_changed) s->binding_changed(i, "");
        }
    }
    std::snprintf(s->binding[slot], sizeof(s->binding[slot]), "%s", key);
    s->ephemeral[slot] = false;
    if (s->binding_changed) s->binding_changed(slot, s->binding[slot]);
}

// Slot for an identified device: its existing binding when free, else
// the first unbound free slot (binding created), else the first bound
// but free slot (binding replaced).
static int8_t slot_for_key(BridgeState* s, const char* key, const char* alt) {
    for (uint8_t i = 0; i < s->numSlots; ++i) {
        if (s->slotToHostIdx[i] < 0 && !s->slots[i].active &&
            (std::strcmp(s->binding[i], key) == 0 ||
             (alt && std::strcmp(s->binding[i], alt) == 0))) {
            return (int8_t)i;
        }
    }
    for (uint8_t i = 0; i < s->numSlots; ++i) {
        if (s->slotToHostIdx[i] < 0 && !s->slots[i].active &&
            s->binding[i][0] == '\0') {
            set_binding(s, i, key);
            return (int8_t)i;
        }
    }
    for (uint8_t i = 0; i < s->numSlots; ++i) {
        if (s->slotToHostIdx[i] < 0 && !s->slots[i].active) {
            set_binding(s, i, key);
            return (int8_t)i;
        }
    }
    return -1;
}

static void try_place(BridgeState* s, uint8_t idx) {
    if (s->hostIdxToSlot[idx] >= 0) return;
    if (!s->host.isDeviceMounted(idx)) return;

    const auto& id = s->host.identity(idx);
    const char* key = identity_key(id);
    if (key) {
        // A binding made under the other key (name vs Product Instance
        // Id, whichever completed first on an earlier boot) still finds
        // its slot; the binding upgrades to the preferred key later.
        const char* alt = (id.endpointName[0] && id.endpointNameComplete &&
                           std::strcmp(id.endpointName, key) != 0)
                            ? id.endpointName : nullptr;
        int8_t slot = slot_for_key(s, key, alt);
        if (slot >= 0) occupy_slot(s, (uint8_t)slot, idx);
        return;
    }

    // Identity absent. Without a clock, place immediately at the first
    // free slot (legacy order-based behaviour); with one, wait out the
    // settle window first. Ephemeral placement prefers unbound slots so
    // it never squats on someone's reserved Function Block.
    if (s->now && s->placeTimeoutMs != 0 &&
        (uint32_t)(s->now() - s->mountedAtMs[idx]) < s->placeTimeoutMs) {
        return;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (uint8_t i = 0; i < s->numSlots; ++i) {
            if (s->slotToHostIdx[i] >= 0 || s->slots[i].active) continue;
            if (pass == 0 && s->binding[i][0] != '\0') continue;
            s->ephemeral[i] = true;
            occupy_slot(s, i, idx);
            return;
        }
    }
}

// Groups left over after the slot windows belong to the bridge itself:
// its MIDI-CI answers there (feedDeviceRx never forwards them), so a
// topology that reserves groups gets one extra Function Block, active
// and named after the endpoint, where the bridge itself is discoverable.
// A 16-group topology (4x4) has no leftover: the endpoint is fully
// delegated and the bridge stays CI-silent.
static uint8_t bridge_fb_first_group(const BridgeState* s) {
    return (uint8_t)(s->numSlots * s->groupsPerSlot);
}

static uint8_t bridge_fb_num_groups(const BridgeState* s) {
    uint8_t first = bridge_fb_first_group(s);
    return (uint8_t)(first < 16 ? 16 - first : 0);
}

static void push_bridge_fb_info(BridgeState* s) {
    uint8_t ng = bridge_fb_num_groups(s);
    if (ng == 0) return;
    s->device.sendFbInfo(/*active*/      true,
                         /*fb_num*/      s->numSlots,
                         /*direction*/   0x03,
                         /*ui_hint*/     0x03,
                         /*first_group*/ bridge_fb_first_group(s),
                         /*num_groups*/  ng,
                         /*midi_ci_ver*/ 0x02,
                         /*sysex8*/      false,
                         /*protocol*/    0x02);
}

static void push_bridge_fb_name(BridgeState* s) {
    if (bridge_fb_num_groups(s) == 0) return;
    s->device.sendFbNameUpdate(s->numSlots, s->endpointName);
}

static void install_stream_responder(BridgeState* s) {
    s->device.onEndpointDiscovery([s](uint8_t filter) {
        if (filter & 0x01) {
            uint8_t num_fb = (uint8_t)(s->numSlots
                                       + (bridge_fb_num_groups(s) ? 1 : 0));
            s->device.sendEndpointInfo(/*ump_ver_major*/ 1,
                                       /*ump_ver_minor*/ 1,
                                       /*static_fb*/    false,
                                       /*num_fb*/       num_fb,
                                       /*midi2*/        true,
                                       /*midi1*/        true,
                                       /*rx_jr*/        false,
                                       /*tx_jr*/        false);
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
            if (filter & 0x01) push_bridge_fb_info(s);
            if (filter & 0x02) push_bridge_fb_name(s);
        } else if (fbNum < s->numSlots) {
            if (filter & 0x01) push_fb_info(s, fbNum);
            if (filter & 0x02) push_fb_name(s, fbNum);
        } else if (fbNum == s->numSlots) {
            if (filter & 0x01) push_bridge_fb_info(s);
            if (filter & 0x02) push_bridge_fb_name(s);
        }
    });

    s->device.onStreamConfigRequest([s](uint8_t protocol) {
        s->device.sendStreamConfigNotify(protocol);
    });
}

static void install_host_callbacks(BridgeState* s) {
    s->host.onDeviceConnected([s](uint8_t idx, const Host::DeviceIdentity&) {
        if (idx >= MIDI2CPP_HOST_MAX_DEVICES) return;
        s->mountedAtMs[idx] = s->now ? s->now() : 0;
        try_place(s, idx);   // places immediately when identity is known
    });
    s->host.onDeviceDisconnected([s](uint8_t idx) {
        if (idx >= MIDI2CPP_HOST_MAX_DEVICES) return;
        int8_t slot = s->hostIdxToSlot[idx];
        if (slot < 0) return;
        s->hostIdxToSlot[idx]   = -1;
        s->slotToHostIdx[slot]  = -1;
        s->slots[slot].active   = false;
        s->slots[slot].name[0]  = '\0';
        push_fb_info(s, (uint8_t)slot);
        push_fb_name(s, (uint8_t)slot);
    });
    s->host.onIdentityUpdated([s](uint8_t idx, const Host::DeviceIdentity& id) {
        if (idx >= MIDI2CPP_HOST_MAX_DEVICES) return;
        int8_t slot = s->hostIdxToSlot[idx];
        if (slot < 0) {
            try_place(s, idx);
            return;
        }
        // Adopt or upgrade the binding: an ephemeral placement takes the
        // key once it shows up, and a name-keyed binding upgrades to the
        // Product Instance Id when that completes, so every device
        // converges on its stable key regardless of reply order.
        const char* key = identity_key(id);
        if (key && std::strcmp(s->binding[(uint8_t)slot], key) != 0 &&
            (s->ephemeral[(uint8_t)slot] ||
             (id.endpointName[0] && id.endpointNameComplete &&
              std::strcmp(s->binding[(uint8_t)slot], id.endpointName) == 0))) {
            set_binding(s, (uint8_t)slot, key);
        }
        // Push the FB Name only for a COMPLETE text that actually
        // changed: identity updates fire per fragment, and hosts that
        // snapshot names must never see a mid-assembly string.
        if (!id.endpointName[0] || !id.endpointNameComplete) return;
        if (std::strcmp(s->slots[(uint8_t)slot].name, id.endpointName) == 0)
            return;
        std::snprintf(s->slots[(uint8_t)slot].name,
                      sizeof(s->slots[(uint8_t)slot].name),
                      "%s", id.endpointName);
        push_fb_name(s, (uint8_t)slot);
    });
}

// ---------------------------------------------------------------------
// Class members.
// ---------------------------------------------------------------------

Bridge::Bridge() {
    auto* s = new BridgeState{};
    for (auto& m : s->hostIdxToSlot) m = -1;
    for (auto& m : s->slotToHostIdx) m = -1;
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
    // Devices still waiting for an identity get placed here, either as
    // soon as the identity lands or ephemerally after the timeout.
    for (uint8_t i = 0; i < MIDI2CPP_HOST_MAX_DEVICES; ++i) {
        if (s->hostIdxToSlot[i] < 0) try_place(s, i);
    }
    s->device.task();
    s->host.task();
}

void Bridge::bindSlot(uint8_t slot, const char* key) {
    auto* s = st(_state);
    if (slot >= s->numSlots || !key) return;
    std::snprintf(s->binding[slot], sizeof(s->binding[slot]), "%s", key);
    s->ephemeral[slot] = false;
}

void Bridge::setPlacementTimeoutMs(uint32_t ms) {
    st(_state)->placeTimeoutMs = ms;
}

void Bridge::onSlotBindingChanged(SlotBindingChangedFn fn) {
    st(_state)->binding_changed = std::move(fn);
}

void Bridge::onTraffic(TrafficFn fn) {
    st(_state)->traffic_tap = std::move(fn);
}

const char* Bridge::slotBinding(uint8_t slot) const {
    auto* s = st(_state);
    return (slot < Bridge::MAX_SLOTS) ? s->binding[slot] : "";
}

int8_t Bridge::slotForHostIdx(uint8_t idx) const {
    auto* s = st(_state);
    return (idx < MIDI2CPP_HOST_MAX_DEVICES) ? s->hostIdxToSlot[idx] : -1;
}

bool Bridge::slotActive(uint8_t slot) const {
    auto* s = st(_state);
    return slot < s->numSlots && s->slots[slot].active;
}

void Bridge::slotSetActive(uint8_t idx, bool active, uint8_t alt) {
    auto* s = st(_state);
    if (idx >= s->numSlots) return;
    // Identity-placed slots belong to the bridge's own placement; the
    // platform only manages slots it allocated itself (MIDI 1.0).
    if (s->slotToHostIdx[idx] >= 0) return;
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

        // Downstream routing, the mirror of forward_ump_to_pc: PC-side
        // traffic addressed to an ACTIVE slot's group window (CVM, SysEx7
        // MIDI-CI, SysEx8, Flex) is rewritten into the device's local
        // groups and forwarded upstream, exclusively: broadcast MIDI-CI
        // (Discovery) is not MUID-filterable, so feeding the internal
        // face too would stamp the bridge's own MUID into every
        // forwarded block. The bridge's CI answers only on groups
        // outside active windows; Stream (0xF) and utility (0x0) are
        // groupless and always stay with the bridge.
        bool routed = false;
        if (mt != 0x0 && mt != 0xE && mt != 0xF && s->upstream_write) {
            uint8_t in_group = (uint8_t)((words[i] >> 24) & 0x0F);
            uint8_t slot     = (uint8_t)(in_group / s->groupsPerSlot);
            if (slot < s->numSlots && s->slots[slot].active) {
                if (s->slotToHostIdx[slot] >= 0) {
                    uint32_t out[4];
                    out[0] = (words[i] & 0xF0FFFFFFu)
                           | ((uint32_t)(in_group % s->groupsPerSlot) << 24);
                    for (uint8_t w = 1; w < wc; ++w) out[w] = words[i + w];
                    s->upstream_write((uint8_t)s->slotToHostIdx[slot], out, wc);
                    if (s->traffic_tap) s->traffic_tap(false, out, wc);
                }
                // An active window always belongs to its device. A
                // platform-managed MIDI 1.0 slot has no UMP return path
                // yet, so its PC traffic is dropped here rather than
                // leaking into the bridge's own responder.
                routed = true;
            }
        }

        if (!routed) s->device.feedRx(&words[i], wc);
        i += wc;
    }
}

void Bridge::feedHostRx(uint8_t idx, const uint32_t* words, size_t count) {
    auto* s = st(_state);
    if (!words || count == 0) return;
    // idx is a HOST DEVICE index, not a slot: bound by the host table.
    if (idx >= MIDI2CPP_HOST_MAX_DEVICES) return;

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

    // Bytes arrive as USB-MIDI 1.0 packets (4-byte CIN-encoded). Channel
    // voice packets are lifted straight to MT 0x2 with the CABLE nibble
    // mapped into the slot's group window, so a multi-port interface
    // keeps its ports apart. Byte-stream parsing (running status, SysEx)
    // runs on cable 0 only: one converter per slot cannot survive
    // interleaved cables, so other cables' non-CVM traffic is dropped.
    const uint8_t base = (uint8_t)(idx * s->groupsPerSlot);
    size_t off = 0;
    while (off + 4 <= count) {
        uint8_t cable = (uint8_t)(bytes[off] >> 4);
        uint8_t cin   = (uint8_t)(bytes[off] & 0x0F);
        if (cin >= 0x8 && cin <= 0xE) {
            uint8_t group = (uint8_t)(base + (cable % s->groupsPerSlot));
            uint32_t w = ((uint32_t)0x2 << 28)
                       | ((uint32_t)group << 24)
                       | ((uint32_t)bytes[off + 1] << 16)
                       | ((uint32_t)bytes[off + 2] << 8)
                       |  (uint32_t)bytes[off + 3];
            if (s->downstream_write) {
                (void)s->downstream_write(&w, 1);
                if (s->traffic_tap) s->traffic_tap(true, &w, 1);
            }
        } else if (cable == 0) {
            uint8_t bcount = kCinByteCount[cin];
            for (uint8_t b = 0; b < bcount; ++b) {
                (void)conv->feed(bytes[off + 1 + b]);
            }
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

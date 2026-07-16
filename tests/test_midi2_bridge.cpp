// tests/test_midi2_bridge.cpp -- m2bridge smoke + group rewrite + heap balance.
//
// Bridge is the only midi2cpp class that allocates state on the heap
// (BridgeState in begin's predecessor; ByteStreamConverter slots inside
// begin). Running this suite under ASan + UBSan is what catches the
// allocate-without-free regressions m2bridge could otherwise hide.
#include "test_common.h"
#include "midi2cpp.h"

#include <cstdlib>
#include <cstring>

uint32_t g_captured_tx[CAPTURE_MAX] = {0};
size_t   g_captured_tx_len = 0;
uint32_t g_test_now_ms = 0;

using namespace midi2;

// Bridge has its own write-fn signatures (size_t return; upstream is
// slot-aware). Local capture wrappers feed test_common's TX buffer.
static size_t bridge_capture_downstream(const uint32_t* w, size_t n) {
    capture_write(w, n);
    return n;
}

static uint8_t  g_last_upstream_idx = 0xFF;
static uint32_t g_upstream_tx[CAPTURE_MAX] = {0};
static size_t   g_upstream_tx_len = 0;

static size_t bridge_capture_upstream(uint8_t idx, const uint32_t* w, size_t n) {
    g_last_upstream_idx = idx;
    for (size_t i = 0; i < n && g_upstream_tx_len < CAPTURE_MAX; ++i)
        g_upstream_tx[g_upstream_tx_len++] = w[i];
    return n;
}

static void upstream_reset() {
    g_last_upstream_idx = 0xFF;
    g_upstream_tx_len = 0;
    std::memset(g_upstream_tx, 0, sizeof(g_upstream_tx));
}

// Helper: build an MT 0x4 NoteOn UMP into a 2-word buffer.
//   word0 = 0x4|G|0x9|CH | NN | 0
//   word1 = VEL16 << 16
static void make_note_on(uint32_t* w, uint8_t group, uint8_t ch,
                         uint8_t note, uint16_t vel16) {
    w[0] = (uint32_t)(0x4u << 28)
         | ((uint32_t)(group & 0x0F) << 24)
         | (uint32_t)(0x9u << 20)
         | ((uint32_t)(ch & 0x0F) << 16)
         | ((uint32_t)note << 8);
    w[1] = (uint32_t)vel16 << 16;
}

// ---------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------

static void test_bridge_constructs_clean(void) {
    TEST("Bridge default-constructs with sane topology defaults");
    m2bridge br;
    CHECK_EQ(br.numSlots(), m2bridge::MAX_SLOTS, "numSlots starts at MAX_SLOTS");
    CHECK_EQ(br.groupsPerSlot(), 4u, "groupsPerSlot defaults to 4");
    PASS();
}

static void test_bridge_destruct_balanced(void) {
    TEST("Bridge construct + begin + destruct is heap-balanced");
    {
        m2bridge br;
        br.setNumSlots(4);
        br.setGroupsPerSlot(4);
        br.setDownstreamWriteFn(bridge_capture_downstream);
        br.setUpstreamWriteFn(bridge_capture_upstream);
        br.setNowFn(test_now_fn);
        br.setRngFn([] { return 0xDEADBEEFu; });
        br.begin();
        br.task();
    }  // dtor must release BridgeState + 4 ByteStreamConverters.
    PASS();
}

static void test_bridge_topology_setters_respect_bounds(void) {
    TEST("setNumSlots / setGroupsPerSlot reject out-of-range");
    m2bridge br;
    uint8_t before_slots = br.numSlots();
    uint8_t before_gps   = br.groupsPerSlot();

    br.setNumSlots(0);                       // rejected
    CHECK_EQ(br.numSlots(), before_slots, "numSlots(0) ignored");
    br.setNumSlots(m2bridge::MAX_SLOTS + 1); // rejected
    CHECK_EQ(br.numSlots(), before_slots, "numSlots(>MAX) ignored");
    br.setNumSlots(2);
    CHECK_EQ(br.numSlots(), 2u, "numSlots(2) accepted");

    br.setGroupsPerSlot(0);                  // rejected
    CHECK_EQ(br.groupsPerSlot(), before_gps, "groupsPerSlot(0) ignored");
    br.setGroupsPerSlot(17);                 // rejected
    CHECK_EQ(br.groupsPerSlot(), before_gps, "groupsPerSlot(17) ignored");
    br.setGroupsPerSlot(8);
    CHECK_EQ(br.groupsPerSlot(), 8u, "groupsPerSlot(8) accepted");
    PASS();
}

static void test_bridge_begin_requires_write_fns(void) {
    TEST("Bridge::begin is a no-op when write fns are missing");
    m2bridge br;
    // Intentionally do NOT call setDownstreamWriteFn / setUpstreamWriteFn.
    br.begin();
    // Topology must remain mutable post-no-op begin (begun stayed false).
    br.setNumSlots(3);
    CHECK_EQ(br.numSlots(), 3u, "numSlots still mutable after no-op begin");
    PASS();
}

static void test_bridge_topology_locks_after_begin(void) {
    TEST("Topology setters are locked once begin() succeeds");
    m2bridge br;
    br.setNumSlots(4);
    br.setGroupsPerSlot(4);
    br.setDownstreamWriteFn(bridge_capture_downstream);
    br.setUpstreamWriteFn(bridge_capture_upstream);
    br.setNowFn(test_now_fn);
    br.begin();
    br.setNumSlots(2);                  // ignored
    CHECK_EQ(br.numSlots(), 4u, "numSlots locked at 4 after begin");
    br.setGroupsPerSlot(8);             // ignored
    CHECK_EQ(br.groupsPerSlot(), 4u, "groupsPerSlot locked at 4 after begin");
    PASS();
}

// ---------------------------------------------------------------------
// Group rewrite
// ---------------------------------------------------------------------
//
// Implementation rule (forward_ump_to_pc):
//   base       = idx * groupsPerSlot
//   out_group  = base + (in_group % groupsPerSlot)
//
// MT 0x0 / 0xE / 0xF are skipped (utility, reserved, stream).

static void make_bridge(m2bridge& br) {
    br.setNumSlots(4);
    br.setGroupsPerSlot(4);
    br.setDownstreamWriteFn(bridge_capture_downstream);
    br.setUpstreamWriteFn(bridge_capture_upstream);
    br.setNowFn(test_now_fn);
    br.setRngFn([] { return 0xCAFEBABEu; });
    br.begin();
    br.setDeviceMounted(true);
    br.setDeviceAltSetting(1);
}

// Find a NoteOn (MT 0x4, status 0x9X) in the captured TX. Stream/JR
// traffic from begin/slotSetActive can also land in the buffer, so the
// test scans rather than peeking [0].
static bool find_first_note_on(uint8_t* out_group) {
    for (size_t i = 0; i < g_captured_tx_len; ++i) {
        uint8_t mt     = (uint8_t)((g_captured_tx[i] >> 28) & 0x0F);
        uint8_t status = (uint8_t)((g_captured_tx[i] >> 20) & 0x0F);
        if (mt == 0x4 && status == 0x9) {
            *out_group = (uint8_t)((g_captured_tx[i] >> 24) & 0x0F);
            return true;
        }
    }
    return false;
}

// Single complete UMP Stream text packet (format 0) with the given
// status (0x003 = Endpoint Name, 0x004 = Product Instance Id).
static void make_stream_text(uint32_t* w, uint16_t status, const char* text) {
    std::memset(w, 0, 16);
    w[0] = (0xFu << 28) | ((uint32_t)status << 16);
    uint8_t len = (uint8_t)std::strlen(text);
    if (len > 14) len = 14;
    for (uint8_t i = 0; i < len; ++i) {
        if (i < 2) {
            w[0] |= ((uint32_t)(uint8_t)text[i]) << (8 - i * 8);
        } else {
            uint8_t off = (uint8_t)(i - 2);
            w[1 + off / 4] |= ((uint32_t)(uint8_t)text[i]) << (24 - (off % 4) * 8);
        }
    }
}

static void mount_midi2(m2bridge& br, uint8_t idx) {
    br.host().notifyDeviceMounted(idx, /*proto*/ 1, /*cables*/ 1,
                                  /*alt*/ 1, /*bcdMSC*/ 0x0200);
    br.task();
}

static void send_pid(m2bridge& br, uint8_t idx, const char* pid) {
    uint32_t w[4];
    make_stream_text(w, 0x004, pid);
    br.feedHostRx(idx, w, 4);
    br.task();
}

// Mount a MIDI 2.0 device at host idx and land it on the given slot via
// an identity binding (the UMP data path resolves slots by identity).
static void mount_on_slot(m2bridge& br, uint8_t idx, uint8_t slot,
                          const char* pid) {
    br.bindSlot(slot, pid);
    mount_midi2(br, idx);
    send_pid(br, idx, pid);
}

static void test_bridge_group_rewrite_slot0(void) {
    TEST("Group rewrite slot 0 (base=0): in_group 7 -> out_group 3");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    mount_on_slot(br, 0, 0, "DEV-A");
    capture_reset();  // discard Stream traffic from mount + placement

    uint32_t note_on[2];
    make_note_on(note_on, /*group*/ 7, /*ch*/ 0, /*note*/ 60, /*vel16*/ 0xFFFF);
    br.feedHostRx(0, note_on, 2);

    uint8_t fwd_group = 0xFF;
    CHECK(find_first_note_on(&fwd_group), "NoteOn appears on downstream");
    CHECK_EQ(fwd_group, 3u, "group 7 % 4 = 3, base 0 -> 3");
    PASS();
}

static void test_bridge_group_rewrite_slot1(void) {
    TEST("Group rewrite slot 1 (base=4): in_group 0 -> out_group 4");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    mount_on_slot(br, 1, 1, "DEV-B");
    capture_reset();

    uint32_t note_on[2];
    make_note_on(note_on, /*group*/ 0, /*ch*/ 5, /*note*/ 64, /*vel16*/ 0x8000);
    br.feedHostRx(1, note_on, 2);

    uint8_t fwd_group = 0xFF;
    CHECK(find_first_note_on(&fwd_group), "NoteOn appears on downstream");
    CHECK_EQ(fwd_group, 4u, "base 4 + (0 % 4) = 4");
    PASS();
}

static void test_bridge_group_rewrite_slot3_max(void) {
    TEST("Group rewrite slot 3 (base=12): in_group 11 -> out_group 15");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    mount_on_slot(br, 3, 3, "DEV-D");
    capture_reset();

    uint32_t note_on[2];
    make_note_on(note_on, /*group*/ 11, /*ch*/ 0, /*note*/ 36, /*vel16*/ 0xC000);
    br.feedHostRx(3, note_on, 2);

    uint8_t fwd_group = 0xFF;
    CHECK(find_first_note_on(&fwd_group), "NoteOn appears on downstream");
    CHECK_EQ(fwd_group, 15u, "base 12 + (11 % 4) = 15");
    PASS();
}

static void test_bridge_drops_out_of_range_slot(void) {
    TEST("feedHostRx with idx >= numSlots is a no-op");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    capture_reset();

    uint32_t note_on[2];
    make_note_on(note_on, 0, 0, 60, 0xFFFF);
    br.feedHostRx(/*idx*/ 99, note_on, 2);

    uint8_t fwd_group = 0xFF;
    CHECK(!find_first_note_on(&fwd_group), "no NoteOn forwarded for idx 99");
    PASS();
}

// ---------------------------------------------------------------------
// MIDI 1.0 byte-stream uplift (alt 0)
// ---------------------------------------------------------------------

static void test_bridge_midi1_bytes_become_mt2_in_slot_window(void) {
    TEST("feedHostMidi1Bytes uplifts USB-MIDI 1.0 packets to MT 0x2 UMPs");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    br.slotSetActive(2, true, /*alt*/ 0);  // legacy upstream
    capture_reset();

    // USB-MIDI 1.0 packet: CIN 0x9 (NoteOn), CN 0, status/data triple.
    // base for slot 2 with groupsPerSlot=4 is 8, so the resulting MT 0x2
    // UMP must land in group 8.
    const uint8_t pkt[4] = {0x09, 0x90, 0x3C, 0x40};  // ch=0 note=60 vel=64
    br.feedHostMidi1Bytes(2, pkt, sizeof(pkt));

    bool   found = false;
    uint8_t group = 0xFF;
    for (size_t i = 0; i < g_captured_tx_len; ++i) {
        uint8_t mt     = (uint8_t)((g_captured_tx[i] >> 28) & 0x0F);
        uint8_t status = (uint8_t)((g_captured_tx[i] >> 20) & 0x0F);
        if (mt == 0x2 && status == 0x9) {
            group = (uint8_t)((g_captured_tx[i] >> 24) & 0x0F);
            found = true;
            break;
        }
    }
    CHECK(found, "MT 0x2 NoteOn emitted by ByteStreamConverter");
    CHECK_EQ(group, 8u, "MT 0x2 lands in slot 2's first group (= 8)");
    PASS();
}

// ---------------------------------------------------------------------
// Stress: heap balance under repeated lifecycles
// ---------------------------------------------------------------------

static void test_bridge_repeated_construct_destroy(void) {
    TEST("50x construct/begin/destroy cycles are heap-balanced");
    for (int i = 0; i < 50; ++i) {
        m2bridge br;
        br.setNumSlots(4);
        br.setGroupsPerSlot(4);
        br.setDownstreamWriteFn(bridge_capture_downstream);
        br.setUpstreamWriteFn(bridge_capture_upstream);
        br.setNowFn(test_now_fn);
        br.setRngFn([] { return 0xAAAAAAAAu; });
        br.begin();
        br.task();
    }
    PASS();
}

// ---------------------------------------------------------------------


// ---- Downstream routing: PC-side traffic into the slot group windows ----

static void test_bridge_downstream_route_cvm(void) {
    TEST("feedDeviceRx routes a CVM on group 5 to slot 1, group 1");
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    mount_on_slot(br, 1, 1, "DEV-B");
    upstream_reset();   // discard anything from activation

    uint32_t note_on[2];
    make_note_on(note_on, /*group*/ 5, /*ch*/ 0, /*note*/ 64, /*vel16*/ 0x8000);
    br.feedDeviceRx(note_on, 2);

    CHECK(g_upstream_tx_len == 2, "one 2-word packet forwarded upstream");
    CHECK(g_last_upstream_idx == 1, "landed on slot 1");
    CHECK(((g_upstream_tx[0] >> 24) & 0x0F) == 1, "group rewritten 5 -> 1");
    CHECK((g_upstream_tx[0] & 0x00FFFFFF) == (note_on[0] & 0x00FFFFFF),
          "payload bits preserved");
    PASS();
}

static void test_bridge_downstream_route_sysex7(void) {
    TEST("feedDeviceRx routes SysEx7 (CI) on group 0 to slot 0, group 0");
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    mount_on_slot(br, 0, 0, "DEV-A");
    upstream_reset();

    // Minimal single-packet SysEx7 on group 0
    uint32_t sx[2] = { (0x3u << 28) | (0x0u << 24) | (0x0u << 20) | (2u << 16) |
                       (0x7Eu << 8) | 0x7Fu, 0x0D700000u };
    br.feedDeviceRx(sx, 2);

    CHECK(g_upstream_tx_len == 2, "SysEx7 packet forwarded upstream");
    CHECK(g_last_upstream_idx == 0, "landed on slot 0");
    CHECK(((g_upstream_tx[0] >> 24) & 0x0F) == 0, "group stays 0 in slot window");
    PASS();
}

// Build a CI Discovery Inquiry (broadcast) and fragment it into SysEx7
// UMPs on the given group, collecting the words into out[].
static size_t make_ci_discovery_ump(uint8_t group, uint32_t* out, size_t cap) {
    uint8_t sysex[64];
    uint16_t len = midi2_ci_build_discovery(
        sysex, MIDI2_CI_VERSION_2, /*src_muid*/ 0x0123456u,
        /*mfr_id*/ 0x7D0000u, /*family*/ 1, /*model*/ 1, /*sw_rev*/ 0x00010000,
        /*ci_category*/ 0x1C, /*max_sysex*/ 512, /*output_path_id*/ 0);

    struct Sink { uint32_t* buf; size_t len; size_t cap; } sink{out, 0, cap};
    midi2_proc_send_sysex7(group, sysex, len,
        [](const uint32_t* w, uint32_t n, void* ctx) -> uint32_t {
            auto* sk = static_cast<Sink*>(ctx);
            for (uint32_t i = 0; i < n && sk->len < sk->cap; ++i)
                sk->buf[sk->len++] = w[i];
            return n;
        }, &sink);
    return sink.len;
}

// Scan the downstream capture for any SysEx7 packet (MT 0x3): after a
// capture_reset, that is the signature of the bridge's own CI replying.
static bool downstream_has_sysex7(void) {
    size_t i = 0;
    while (i < g_captured_tx_len) {
        uint8_t mt = (uint8_t)((g_captured_tx[i] >> 28) & 0x0F);
        if (mt == 0x3) return true;
        static const uint8_t wc[16] = {1,1,1,2, 2,4,1,1, 2,2,2,3, 3,4,4,4};
        i += wc[mt];
    }
    return false;
}

static void test_bridge_downstream_active_window_is_exclusive(void) {
    TEST("CI Discovery on an active slot window is not answered by the bridge");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    mount_on_slot(br, 1, 1, "DEV-B");
    capture_reset();
    upstream_reset();

    // Broadcast Discovery on group 5 = slot 1's window. It must go
    // upstream to the device, and the bridge's own CI must stay silent:
    // one Discovery, one reply, one MUID per block.
    uint32_t ump[16];
    size_t n = make_ci_discovery_ump(/*group*/ 5, ump, 16);
    CHECK(n >= 2, "forged Discovery spans at least one SysEx7 packet");
    br.feedDeviceRx(ump, n);

    CHECK(g_upstream_tx_len > 0, "Discovery forwarded upstream to slot 1");
    CHECK_EQ(g_last_upstream_idx, 1u, "landed on slot 1");
    CHECK(!downstream_has_sysex7(), "bridge CI does not reply in a forwarded window");
    PASS();
}

static void test_bridge_downstream_inactive_window_reaches_bridge_ci(void) {
    TEST("CI Discovery outside active windows still reaches the bridge's CI");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    mount_on_slot(br, 1, 1, "DEV-B");
    capture_reset();
    upstream_reset();

    // Group 9 = slot 2's window, inactive: nothing upstream, and the
    // bridge's own CI answers there (this is where the bridge lives).
    uint32_t ump[16];
    size_t n = make_ci_discovery_ump(/*group*/ 9, ump, 16);
    br.feedDeviceRx(ump, n);

    CHECK(g_upstream_tx_len == 0, "inactive window is not forwarded");
    CHECK(downstream_has_sysex7(), "bridge CI replies on its own turf");
    PASS();
}

static void test_bridge_downstream_skips_stream_and_inactive(void) {
    TEST("feedDeviceRx keeps Stream local and skips inactive slots");
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    mount_on_slot(br, 0, 0, "DEV-A");
    upstream_reset();

    // Stream Endpoint Discovery (MT 0xF): must NOT go upstream.
    uint32_t ep_disc[4] = { (0xFu << 28) | (0x000u << 16) | (0x01u << 8) | 0x01u,
                            0x1F, 0, 0 };
    br.feedDeviceRx(ep_disc, 4);
    CHECK(g_upstream_tx_len == 0, "Stream stays with the bridge");

    // CVM on group 9 -> slot 2, which is inactive: dropped, not forwarded.
    uint32_t note_on[2];
    make_note_on(note_on, /*group*/ 9, /*ch*/ 0, /*note*/ 60, /*vel16*/ 0x4000);
    br.feedDeviceRx(note_on, 2);
    CHECK(g_upstream_tx_len == 0, "inactive slot window is not forwarded");
    PASS();
}

// ---- Bridge's own Function Block (leftover groups) ----

// Scan downstream capture for a Stream message with the given status;
// when fb_num >= 0, only match FB Info/Name packets for that block.
static bool find_stream_msg(uint16_t status, int fb_num,
                            uint32_t* out_w0, uint32_t* out_w1) {
    static const uint8_t wc[16] = {1,1,1,2, 2,4,1,1, 2,2,2,3, 3,4,4,4};
    size_t i = 0;
    while (i < g_captured_tx_len) {
        uint8_t mt = (uint8_t)((g_captured_tx[i] >> 28) & 0x0F);
        if (mt == 0xF) {
            uint16_t st = (uint16_t)((g_captured_tx[i] >> 16) & 0x3FF);
            uint8_t  fb = (uint8_t)((g_captured_tx[i] >> 8) & 0x7F);
            if (st == status && (fb_num < 0 || fb == (uint8_t)fb_num)) {
                if (out_w0) *out_w0 = g_captured_tx[i];
                if (out_w1) *out_w1 = g_captured_tx[i + 1];
                return true;
            }
        }
        i += wc[mt];
    }
    return false;
}

static void make_bridge_n(m2bridge& br, uint8_t nslots) {
    br.setNumSlots(nslots);
    br.setGroupsPerSlot(4);
    br.setDownstreamWriteFn(bridge_capture_downstream);
    br.setUpstreamWriteFn(bridge_capture_upstream);
    br.setNowFn(test_now_fn);
    br.setRngFn([] { return 0xCAFEBABEu; });
    br.begin();
    br.setDeviceMounted(true);
    br.setDeviceAltSetting(1);
}

static void test_bridge_own_fb_advertised_when_groups_remain(void) {
    TEST("3 slots x 4 groups: FB 3 is the bridge's own, active on groups 12-15");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge_n(br, 3);
    capture_reset();

    // Endpoint Discovery (filter 0x01): num_fb must count the bridge FB.
    uint32_t ep_disc[4] = { (0xFu << 28) | (0x000u << 16) | (0x01u << 8) | 0x01u,
                            0x01, 0, 0 };
    br.feedDeviceRx(ep_disc, 4);
    uint32_t w0 = 0, w1 = 0;
    CHECK(find_stream_msg(/*Endpoint Info*/ 0x001, -1, &w0, &w1),
          "Endpoint Info Notify emitted");
    CHECK_EQ((w1 >> 24) & 0x7F, 4u, "num_fb = 3 slots + 1 bridge FB");

    // FB Discovery (all blocks, info + name): FB 3 = the bridge.
    capture_reset();
    uint32_t fb_disc[4] = { (0xFu << 28) | (0x010u << 16) | (0xFFu << 8) | 0x03u,
                            0, 0, 0 };
    br.feedDeviceRx(fb_disc, 4);
    CHECK(find_stream_msg(/*FB Info*/ 0x011, 3, &w0, &w1),
          "FB Info for block 3 emitted");
    CHECK((w0 >> 15) & 0x1, "bridge FB is active");
    CHECK_EQ((w1 >> 24) & 0x0F, 12u, "first_group = 12");
    CHECK_EQ((w1 >> 16) & 0xFF, 4u, "num_groups = 4");
    CHECK(find_stream_msg(/*FB Name*/ 0x012, 3, &w0, &w1),
          "FB Name for block 3 emitted");
    PASS();
}

static void test_bridge_ci_answers_on_its_own_fb(void) {
    TEST("3 slots: CI Discovery on group 12 is answered by the bridge's CI");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge_n(br, 3);
    mount_on_slot(br, 0, 0, "DEV-A");
    mount_on_slot(br, 1, 1, "DEV-B");
    mount_on_slot(br, 2, 2, "DEV-C");
    capture_reset();
    upstream_reset();

    uint32_t ump[16];
    size_t n = make_ci_discovery_ump(/*group*/ 12, ump, 16);
    br.feedDeviceRx(ump, n);

    CHECK(g_upstream_tx_len == 0, "bridge FB groups are never forwarded");
    CHECK(downstream_has_sysex7(), "bridge CI replies on its own FB");
    PASS();
}

static void test_bridge_full_topology_has_no_own_fb(void) {
    TEST("4 slots x 4 groups: fully delegated endpoint advertises 4 FBs only");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge_n(br, 4);
    capture_reset();

    uint32_t ep_disc[4] = { (0xFu << 28) | (0x000u << 16) | (0x01u << 8) | 0x01u,
                            0x01, 0, 0 };
    br.feedDeviceRx(ep_disc, 4);
    uint32_t w0 = 0, w1 = 0;
    CHECK(find_stream_msg(0x001, -1, &w0, &w1), "Endpoint Info Notify emitted");
    CHECK_EQ((w1 >> 24) & 0x7F, 4u, "num_fb = 4, no bridge FB");

    capture_reset();
    uint32_t fb_disc[4] = { (0xFu << 28) | (0x010u << 16) | (0xFFu << 8) | 0x03u,
                            0, 0, 0 };
    br.feedDeviceRx(fb_disc, 4);
    CHECK(!find_stream_msg(0x011, 4, &w0, &w1), "no FB Info for block 4");
    PASS();
}

// ---- Identity-bound slots (stable FB numbers across boots) ----

static void test_bridge_bound_identity_lands_on_its_slot(void) {
    TEST("pre-seeded binding: device lands on its bound slot, any mount order");
    capture_reset();
    upstream_reset();
    test_set_now(1000);
    m2bridge br;
    make_bridge(br);
    br.bindSlot(2, "NRF-001");

    mount_midi2(br, 0);
    CHECK(br.slotForHostIdx(0) < 0, "no slot before the identity arrives");

    send_pid(br, 0, "NRF-001");
    CHECK_EQ(br.slotForHostIdx(0), 2, "identity NRF-001 lands on bound slot 2");

    // Upstream traffic from host idx 0 must come out in slot 2's window.
    capture_reset();
    uint32_t note_on[2];
    make_note_on(note_on, /*group*/ 0, 0, 60, 0xFFFF);
    br.feedHostRx(0, note_on, 2);
    uint8_t fwd_group = 0xFF;
    CHECK(find_first_note_on(&fwd_group), "NoteOn forwarded downstream");
    CHECK_EQ(fwd_group, 8u, "slot 2 window base = group 8");
    PASS();
}

static void test_bridge_new_identity_binds_first_free_and_persists(void) {
    TEST("new identity takes the first unbound slot and fires the persist hook");
    capture_reset();
    upstream_reset();
    test_set_now(1000);
    m2bridge br;
    make_bridge(br);

    static uint8_t     cb_slot;
    static char        cb_key[64];
    cb_slot = 0xFF; cb_key[0] = '\0';
    br.onSlotBindingChanged([](uint8_t slot, const char* key) {
        cb_slot = slot;
        std::snprintf(cb_key, sizeof(cb_key), "%s", key);
    });

    mount_midi2(br, 1);
    send_pid(br, 1, "F411-XYZ");
    CHECK_EQ(br.slotForHostIdx(1), 0, "first unbound slot is 0");
    CHECK_EQ(cb_slot, 0u, "persist hook fired for slot 0");
    CHECK(std::strcmp(cb_key, "F411-XYZ") == 0, "persist hook carries the key");

    // Same identity re-mounting at a DIFFERENT host idx: same slot.
    br.host().notifyDeviceUnmounted(1);
    br.task();
    CHECK(br.slotForHostIdx(1) < 0, "unmount clears the placement");
    mount_midi2(br, 3);
    send_pid(br, 3, "F411-XYZ");
    CHECK_EQ(br.slotForHostIdx(3), 0, "same identity, same slot, new idx");
    PASS();
}

static void test_bridge_unidentified_gets_ephemeral_slot_after_timeout(void) {
    TEST("no identity: ephemeral placement after 3 s, nothing persisted");
    capture_reset();
    upstream_reset();
    test_set_now(1000);
    m2bridge br;
    make_bridge(br);
    br.bindSlot(0, "F411-XYZ");   // slot 0 reserved for someone else

    static bool cb_fired;
    cb_fired = false;
    br.onSlotBindingChanged([](uint8_t, const char*) { cb_fired = true; });

    mount_midi2(br, 0);
    test_set_now(3900);
    br.task();
    CHECK(br.slotForHostIdx(0) < 0, "still waiting at 2.9 s");

    test_set_now(4100);
    br.task();
    CHECK_EQ(br.slotForHostIdx(0), 1, "ephemeral placement skips the bound slot");
    CHECK(!cb_fired, "ephemeral placement is not persisted");
    PASS();
}

static void test_bridge_downstream_routes_via_binding(void) {
    TEST("PC traffic in a bound slot's window reaches the right host idx");
    capture_reset();
    upstream_reset();
    test_set_now(1000);
    m2bridge br;
    make_bridge(br);
    br.bindSlot(2, "NRF-001");
    mount_midi2(br, 0);
    send_pid(br, 0, "NRF-001");
    upstream_reset();

    uint32_t note_on[2];
    make_note_on(note_on, /*group*/ 9, 0, 64, 0x8000);
    br.feedDeviceRx(note_on, 2);
    CHECK(g_upstream_tx_len == 2, "packet forwarded upstream");
    CHECK_EQ(g_last_upstream_idx, 0u, "slot 2 maps back to host idx 0");
    CHECK_EQ((g_upstream_tx[0] >> 24) & 0x0F, 1u, "group 9 -> local group 1");
    PASS();
}

int main(void) {
    std::printf("\n[m2bridge]\n");

    test_bridge_constructs_clean();
    test_bridge_destruct_balanced();
    test_bridge_topology_setters_respect_bounds();
    test_bridge_begin_requires_write_fns();
    test_bridge_topology_locks_after_begin();

    test_bridge_group_rewrite_slot0();
    test_bridge_downstream_route_cvm();
    test_bridge_downstream_route_sysex7();
    test_bridge_downstream_skips_stream_and_inactive();
    test_bridge_downstream_active_window_is_exclusive();
    test_bridge_downstream_inactive_window_reaches_bridge_ci();
    test_bridge_own_fb_advertised_when_groups_remain();
    test_bridge_ci_answers_on_its_own_fb();
    test_bridge_full_topology_has_no_own_fb();
    test_bridge_bound_identity_lands_on_its_slot();
    test_bridge_new_identity_binds_first_free_and_persists();
    test_bridge_unidentified_gets_ephemeral_slot_after_timeout();
    test_bridge_downstream_routes_via_binding();
    test_bridge_group_rewrite_slot1();
    test_bridge_group_rewrite_slot3_max();
    test_bridge_drops_out_of_range_slot();

    test_bridge_midi1_bytes_become_mt2_in_slot_window();

    test_bridge_repeated_construct_destroy();

    REPORT_AND_EXIT();
}

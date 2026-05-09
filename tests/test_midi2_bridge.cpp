// tests/test_midi2_bridge.cpp -- m2bridge smoke + group rewrite + heap balance.
//
// Bridge is the only midi2_cpp class that allocates state on the heap
// (BridgeState in begin's predecessor; ByteStreamConverter slots inside
// begin). Running this suite under ASan + UBSan is what catches the
// allocate-without-free regressions m2bridge could otherwise hide.
#include "test_common.h"
#include "midi2_cpp.h"

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

static void test_bridge_group_rewrite_slot0(void) {
    TEST("Group rewrite slot 0 (base=0): in_group 7 -> out_group 3");
    capture_reset();
    upstream_reset();
    m2bridge br;
    make_bridge(br);
    br.slotSetActive(0, true, 1);
    capture_reset();  // discard FB Info / FB Name from slotSetActive

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
    br.slotSetActive(1, true, 1);
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
    br.slotSetActive(3, true, 1);
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

int main(void) {
    std::printf("\n[m2bridge]\n");

    test_bridge_constructs_clean();
    test_bridge_destruct_balanced();
    test_bridge_topology_setters_respect_bounds();
    test_bridge_begin_requires_write_fns();
    test_bridge_topology_locks_after_begin();

    test_bridge_group_rewrite_slot0();
    test_bridge_group_rewrite_slot1();
    test_bridge_group_rewrite_slot3_max();
    test_bridge_drops_out_of_range_slot();

    test_bridge_midi1_bytes_become_mt2_in_slot_window();

    test_bridge_repeated_construct_destroy();

    REPORT_AND_EXIT();
}

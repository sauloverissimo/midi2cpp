// tests/test_midi2_host.cpp, smoke for m2host skeleton.
//
// Exercises the API surface end-to-end at the call-site level: every
// public method either returns a sensible default or invokes the
// registered callback when appropriate. Real UMP routing logic lands in
// later commits; this smoke locks the surface shape so subsequent
// implementations cannot regress it.
#include "test_common.h"
#include "midi2cpp.h"

#include <cstdlib>
#include <cstring>

uint32_t g_captured_tx[CAPTURE_MAX] = {0};
size_t   g_captured_tx_len = 0;
uint32_t g_test_now_ms = 0;

using namespace midi2;

// ---------- Lifecycle + identity ----------

static void test_host_constructs_clean(void) {
    TEST("Host default-constructs with deviceCount() == 0");
    m2host h;
    CHECK_EQ(h.deviceCount(), 0u, "no devices initially");
    CHECK(!h.isDeviceMounted(0), "idx 0 not mounted");
    CHECK(!h.isDeviceMounted(7), "out-of-range idx safe");
    CHECK_EQ(h.hostMuid(), 0u, "MUID 0 before begin (no RNG)");
    PASS();
}

static void test_host_begin_seeds_muid_via_rng(void) {
    TEST("Host::begin seeds MUID via RngFn (28-bit, non-zero)");
    m2host h;
    h.setRngFn([] { return 0xDEADBEEFu; });
    h.begin();
    uint32_t m = h.hostMuid();
    CHECK(m != 0u, "MUID populated");
    CHECK_EQ(m & 0xF0000000u, 0u, "MUID is 28-bit (top nibble zero)");
    PASS();
}

static void test_host_regenerate_muid_changes_value(void) {
    TEST("Host::regenerateHostMuid produces a different MUID");
    m2host h;
    int call = 0;
    h.setRngFn([&] { return (call++ == 0) ? 0x12345678u : 0x9ABCDEF0u; });
    h.begin();
    uint32_t before = h.hostMuid();
    h.regenerateHostMuid();
    uint32_t after  = h.hostMuid();
    CHECK(before != after, "MUID changed");
    CHECK_EQ(after & 0xF0000000u, 0u, "new MUID still 28-bit");
    PASS();
}

// ---------- Mount lifecycle + connected callback ----------

static void test_host_notify_mount_populates_identity(void) {
    TEST("notifyDeviceMounted sets mounted + protocolVersion + cableCount");
    m2host h;
    h.notifyDeviceMounted(/*idx*/ 0,
                           /*protocolVersion*/ 0x02,
                           /*cableCount*/ 4);
    CHECK(h.isDeviceMounted(0), "idx 0 now mounted");
    CHECK_EQ(h.deviceCount(), 1u, "deviceCount = 1");
    const auto& id = h.identity(0);
    CHECK(id.mounted, "identity.mounted true");
    CHECK_EQ(id.protocolVersion, 0x02u, "protocol stored");
    CHECK_EQ(id.cableCount, 4u, "cableCount stored");
    // Defaults for the optional notify args:
    CHECK_EQ(id.altSettingActive, 1u, "altSettingActive defaults to 1 (MIDI 2.0)");
    CHECK_EQ(id.bcdMSC, 0x0200u,       "bcdMSC defaults to 0x0200");
    PASS();
}

static void test_host_notify_mount_with_alt0_and_bcdMSC(void) {
    TEST("notifyDeviceMounted records altSettingActive + bcdMSC when supplied");
    m2host h;
    h.notifyDeviceMounted(/*idx*/ 1,
                           /*protocolVersion*/ 0x01,
                           /*cableCount*/ 1,
                           /*altSettingActive*/ 0,
                           /*bcdMSC*/ 0x0100);
    const auto& id = h.identity(1);
    CHECK_EQ(id.altSettingActive, 0u, "altSettingActive = 0 (MIDI 1.0 fallback)");
    CHECK_EQ(id.bcdMSC, 0x0100u, "bcdMSC = 0x0100");
    PASS();
}

static void test_host_identity_ci_initiator_state_default_zero(void) {
    TEST("DeviceIdentity CI Initiator state defaults to zero on mount");
    m2host h;
    h.notifyDeviceMounted(2, 0x02, 1);
    const auto& id = h.identity(2);
    CHECK(!id.ciDiscovered,         "ciDiscovered false at mount");
    CHECK(!id.ciDiscoveryPending,   "ciDiscoveryPending false at mount");
    CHECK_EQ(id.ciMuid, 0u,          "ciMuid 0 (no remote MUID known yet)");
    CHECK_EQ(id.ciDiscoveryRequestId, 0u, "no inquiry in flight");
    CHECK_EQ(id.ciDiscoverySentMs, 0u,    "no sent timestamp");
    PASS();
}

static void test_host_unmount_clears_state(void) {
    TEST("notifyDeviceUnmounted clears mounted state, deviceCount drops");
    m2host h;
    h.notifyDeviceMounted(0, 0x02, 1);
    h.notifyDeviceMounted(2, 0x01, 1);
    CHECK_EQ(h.deviceCount(), 2u, "two mounted");
    h.notifyDeviceUnmounted(0);
    CHECK(!h.isDeviceMounted(0), "idx 0 unmounted");
    CHECK(h.isDeviceMounted(2), "idx 2 still mounted");
    CHECK_EQ(h.deviceCount(), 1u, "deviceCount = 1");
    PASS();
}

static void test_host_connected_callback_fires_on_mount(void) {
    TEST("onDeviceConnected callback fires with correct idx + identity");
    m2host h;
    bool fired = false;
    uint8_t fired_idx = 0xFF;
    uint8_t fired_proto = 0;
    h.onDeviceConnected([&](uint8_t idx, const m2host::DeviceIdentity& id) {
        fired = true;
        fired_idx = idx;
        fired_proto = id.protocolVersion;
    });
    h.notifyDeviceMounted(/*idx*/ 3, /*protocolVersion*/ 0x02, /*cables*/ 1);
    CHECK(fired, "callback fired");
    CHECK_EQ(fired_idx, 3u, "idx forwarded");
    CHECK_EQ(fired_proto, 0x02u, "protocolVersion forwarded");
    PASS();
}

static void test_host_disconnected_callback_fires_on_unmount(void) {
    TEST("onDeviceDisconnected callback fires with correct idx");
    m2host h;
    bool fired = false;
    uint8_t fired_idx = 0xFF;
    h.onDeviceDisconnected([&](uint8_t idx) {
        fired = true;
        fired_idx = idx;
    });
    h.notifyDeviceMounted(2, 0x02, 1);
    h.notifyDeviceUnmounted(2);
    CHECK(fired, "disconnected callback fired");
    CHECK_EQ(fired_idx, 2u, "idx forwarded");
    PASS();
}

// ---------- Out-of-range idx safety ----------

static void test_host_out_of_range_idx_safe(void) {
    TEST("Host::notifyDeviceMounted with idx >= MAX_DEVICES is a no-op");
    m2host h;
    int fired = 0;
    h.onDeviceConnected([&](uint8_t, const m2host::DeviceIdentity&) { ++fired; });
    h.notifyDeviceMounted(/*idx*/ m2host::MAX_DEVICES, 0x02, 1);
    h.notifyDeviceMounted(/*idx*/ 0xFF, 0x02, 1);
    CHECK_EQ(fired, 0, "out-of-range mounts ignored");
    CHECK_EQ(h.deviceCount(), 0u, "deviceCount stays 0");
    PASS();
}

// ---------- Senders refuse without write_fn ----------

static void test_host_senders_refuse_without_write_fn(void) {
    TEST("Senders return false when write_fn is unset");
    m2host h;
    h.notifyDeviceMounted(0, 0x02, 1);
    // No setWriteFn, every sender must refuse.
    CHECK(!h.noteOn(0, 0, 60, 0xC000),       "noteOn refused");
    CHECK(!h.noteOff(0, 0, 60),               "noteOff refused");
    CHECK(!h.cc(0, 0, 7, 0x80000000u),        "cc refused");
    CHECK(!h.pitchBend(0, 0, 0x80000000u),    "pitchBend refused");
    CHECK(!h.sendDiscoveryInquiry(0),         "discovery refused");
    PASS();
}

// ---------- Group remap setter / clear ----------

static void test_host_group_remap_setter(void) {
    TEST("Inbound group remap setter / clear are accepted without crashing");
    m2host h;
    uint8_t map[16] = {0};
    for (uint8_t i = 0; i < 16; ++i) map[i] = (uint8_t)(15 - i);
    h.setInboundGroupRemap(0, map);
    h.setInboundGroupRemap(/*invalid*/ m2host::MAX_DEVICES, map);  // no-op
    h.setInboundGroupRemap(0, /*null*/ nullptr);                   // no-op
    h.clearInboundGroupRemap(0);
    h.clearInboundGroupRemap(/*invalid*/ m2host::MAX_DEVICES);     // no-op
    PASS();
}

// ---------- Callback overload resolution (simple + verbose) ----------

static void test_host_callback_overloads_resolve(void) {
    TEST("onNoteOn overloads (verbose + simple) resolve cleanly at call site");
    m2host h;
    int simple_calls = 0;
    int verbose_calls = 0;
    // Verbose form (7 params)
    h.onNoteOn([&](uint8_t, uint8_t, uint8_t, uint8_t, uint16_t,
                    uint8_t, uint16_t) { ++verbose_calls; });
    // Simple form (4 params) - replaces verbose under "latest setter wins"
    h.onNoteOn([&](uint8_t, uint8_t, uint8_t, uint16_t) { ++simple_calls; });
    CHECK_EQ(simple_calls, 0, "no UMP fed yet");
    CHECK_EQ(verbose_calls, 0, "no UMP fed yet");
    PASS();
}

// ---------- Round-trip: feedRx UMP -> typed callback fires with idx ----------

static void test_host_feedRx_dispatches_note_on(void) {
    TEST("feedRx with MT 0x4 NoteOn UMP fires onNoteOn with correct idx + fields");
    m2host h;
    h.begin();
    h.notifyDeviceMounted(/*idx*/ 2, /*proto*/ 0x02, /*cables*/ 1);

    int fired = 0;
    uint8_t got_idx = 0xFF, got_ch = 0xFF, got_note = 0xFF;
    uint16_t got_vel = 0;
    h.onNoteOn([&](uint8_t idx, uint8_t ch, uint8_t note, uint16_t vel) {
        ++fired;
        got_idx = idx; got_ch = ch; got_note = note; got_vel = vel;
    });

    // Build a real MIDI 2.0 NoteOn UMP (group 0, channel 5, note 60, vel 0xC000).
    uint32_t words[2];
    midi2_msg_note_on(words, /*group*/ 0, /*channel*/ 5, /*note*/ 60,
                       /*vel*/ 0xC000, /*attr_type*/ 0, /*attr_data*/ 0);
    h.feedRx(/*idx*/ 2, words, 2);
    h.task();   // feedRx enqueues; dispatch happens when task() drains the ring

    CHECK_EQ(fired, 1, "callback fired exactly once");
    CHECK_EQ(got_idx, 2u, "idx forwarded");
    CHECK_EQ(got_ch, 5u, "channel decoded");
    CHECK_EQ(got_note, 60u, "note decoded");
    CHECK_EQ(got_vel, 0xC000u, "16-bit velocity decoded");
    PASS();
}

static void test_host_feedRx_isolates_idx(void) {
    TEST("feedRx on idx A does not fire callbacks attributed to idx B");
    m2host h;
    h.begin();
    h.notifyDeviceMounted(/*idx*/ 0, 0x02, 1);
    h.notifyDeviceMounted(/*idx*/ 1, 0x02, 1);

    int idx0_count = 0, idx1_count = 0;
    h.onNoteOn([&](uint8_t idx, uint8_t, uint8_t, uint16_t) {
        if (idx == 0) ++idx0_count;
        else if (idx == 1) ++idx1_count;
    });

    uint32_t words[2];
    midi2_msg_note_on(words, 0, 0, 60, 0x4000, 0, 0);
    h.feedRx(0, words, 2);
    h.feedRx(0, words, 2);
    h.feedRx(1, words, 2);
    h.task();   // drain: dispatch all queued packets

    CHECK_EQ(idx0_count, 2, "idx 0 got 2 events");
    CHECK_EQ(idx1_count, 1, "idx 1 got 1 event");
    PASS();
}

// ---------- Stream core: RX ring buffers a burst, task() drains it ----------

static void test_host_rx_ring_buffers_burst_no_loss(void) {
    TEST("a burst of feedRx before task() is fully dispatched, in order, no loss");
    m2host h;
    h.begin();
    h.notifyDeviceMounted(/*idx*/ 0, 0x02, 1);

    int fired = 0;
    int last_note = -1, out_of_order = 0;
    h.onNoteOn([&](uint8_t, uint8_t, uint8_t note, uint16_t) {
        ++fired;
        if (note <= last_note) ++out_of_order;
        last_note = note;
    });

    // Feed a burst WITHOUT draining (the producer never blocks). Stays within
    // the ring capacity, so nothing is dropped.
    const int BURST = 40;
    for (int n = 0; n < BURST; ++n) {
        uint32_t words[2];
        midi2_msg_note_on(words, 0, 0, (uint8_t)n, 0x4000, 0, 0);
        h.feedRx(0, words, 2);
    }
    CHECK_EQ(fired, 0, "nothing dispatched yet (feedRx only enqueues)");

    h.task();   // drain the whole burst

    CHECK_EQ(fired, BURST, "every queued note-on dispatched");
    CHECK_EQ(out_of_order, 0, "dispatched in FIFO order");
    CHECK_EQ(h.rxDropped(), 0u, "no drops within ring capacity");
    PASS();
}

static void test_host_rx_ring_overflow_counts_drops(void) {
    TEST("overfilling the ring without draining counts drops, never corrupts");
    m2host h;
    h.begin();
    h.notifyDeviceMounted(/*idx*/ 0, 0x02, 1);

    int fired = 0;
    h.onNoteOn([&](uint8_t, uint8_t, uint8_t, uint16_t) { ++fired; });

    // Push well past capacity with no drain: excess packets are refused
    // (drop-newest) and counted, the consumer index is never touched.
    const int OVER = MIDI2CPP_HOST_RX_RING + 50;
    for (int n = 0; n < OVER; ++n) {
        uint32_t words[2];
        midi2_msg_note_on(words, 0, 0, 60, 0x4000, 0, 0);
        h.feedRx(0, words, 2);
    }
    CHECK(h.rxDropped() > 0u, "drops counted on overflow");

    h.task();   // drain whatever fit
    CHECK(fired > 0, "buffered packets still dispatched cleanly");
    PASS();
}

// ---------- Senders emit UMP via write_fn ----------

static void test_host_sendNoteOn_emits_via_write_fn(void) {
    TEST("Host::noteOn(idx, ch, note, vel) emits MT 0x4 UMP through write_fn");
    m2host h;

    uint8_t captured_idx = 0xFF;
    h.setWriteFn([&](uint8_t idx, const uint32_t* w, size_t n) {
        captured_idx = idx;
        for (size_t i = 0; i < n && g_captured_tx_len < CAPTURE_MAX; ++i) {
            g_captured_tx[g_captured_tx_len++] = w[i];
        }
    });
    h.begin();
    // Disable auto-discover so notifyDeviceMounted does not flood the
    // capture with Endpoint Discovery / FB Discovery / CI Discovery.
    h.setAutoDiscover(false);
    h.notifyDeviceMounted(/*idx*/ 1, 0x02, 1);
    capture_reset();

    bool ok = h.noteOn(/*idx*/ 1, /*channel*/ 3, /*note*/ 60, /*vel*/ 0xC000);
    CHECK(ok, "noteOn returned true");
    CHECK_EQ(captured_idx, 1u, "write_fn received correct idx");
    CHECK_EQ(g_captured_tx_len, 2u, "2 words emitted (MT 0x4)");
    uint32_t w0 = g_captured_tx[0];
    CHECK_EQ((w0 >> 28) & 0xFu, 0x4u, "MT = 0x4");
    CHECK_EQ((w0 >> 20) & 0xFu, 0x9u, "status nibble = 0x9 (Note On)");
    CHECK_EQ((w0 >> 16) & 0xFu, 0x3u, "channel = 3");
    CHECK_EQ(g_captured_tx[1] >> 16, 0xC000u, "16-bit velocity");
    PASS();
}

static void test_host_sender_blocks_when_unmounted(void) {
    TEST("Senders return false when target idx is not mounted");
    m2host h;
    h.setWriteFn([](uint8_t, const uint32_t*, size_t) {});
    h.begin();
    // No notifyDeviceMounted, so idx 0 stays unmounted.
    CHECK(!h.noteOn(0, 0, 60, 0xC000), "noteOn refused on unmounted idx");
    CHECK(!h.cc(0, 0, 7, 0x80000000u), "cc refused on unmounted idx");
    PASS();
}

// ---------- CI Discovery Inquiry emits a SysEx7 stream ----------

static void test_host_sendDiscoveryInquiry_emits_sysex7(void) {
    TEST("sendDiscoveryInquiry emits MT 0x3 SysEx7 packets and marks pending");
    m2host h;

    capture_reset();
    h.setWriteFn([&](uint8_t /*idx*/, const uint32_t* w, size_t n) {
        for (size_t i = 0; i < n && g_captured_tx_len < CAPTURE_MAX; ++i) {
            g_captured_tx[g_captured_tx_len++] = w[i];
        }
    });
    // Deterministic MUID from a known RNG.
    h.setRngFn([] { return 0xCAFEBABEu; });
    h.begin();

    // notifyDeviceMounted with auto_discover=true (default) already triggers
    // an Endpoint Discovery + Discovery Inquiry. Reset the capture and call
    // sendDiscoveryInquiry explicitly so this test only sees the inquiry.
    h.setAutoDiscover(false);
    h.notifyDeviceMounted(/*idx*/ 0, 0x02, 1);
    capture_reset();

    bool ok = h.sendDiscoveryInquiry(/*idx*/ 0);
    CHECK(ok, "sendDiscoveryInquiry returned true");
    CHECK(g_captured_tx_len >= 2u, "at least one SysEx7 packet emitted (>= 2 words)");
    // First word should be MT 0x3 (SysEx7).
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu, 0x3u, "first word MT = 0x3");

    // Identity should now be in pending state (we await Discovery Reply).
    const auto& id = h.identity(0);
    CHECK(id.ciDiscoveryPending, "ciDiscoveryPending true after inquiry");
    CHECK(id.ciDiscoveryRequestId != 0u, "request id assigned (non-zero)");
    PASS();
}

int main(void) {
    std::srand(42);  // deterministic for any test that uses std::rand

    test_host_constructs_clean();
    test_host_begin_seeds_muid_via_rng();
    test_host_regenerate_muid_changes_value();
    test_host_notify_mount_populates_identity();
    test_host_notify_mount_with_alt0_and_bcdMSC();
    test_host_identity_ci_initiator_state_default_zero();
    test_host_unmount_clears_state();
    test_host_connected_callback_fires_on_mount();
    test_host_disconnected_callback_fires_on_unmount();
    test_host_out_of_range_idx_safe();
    test_host_senders_refuse_without_write_fn();
    test_host_group_remap_setter();
    test_host_callback_overloads_resolve();

    test_host_feedRx_dispatches_note_on();
    test_host_feedRx_isolates_idx();
    test_host_rx_ring_buffers_burst_no_loss();
    test_host_rx_ring_overflow_counts_drops();
    test_host_sendNoteOn_emits_via_write_fn();
    test_host_sender_blocks_when_unmounted();
    test_host_sendDiscoveryInquiry_emits_sysex7();

    REPORT_AND_EXIT();
}

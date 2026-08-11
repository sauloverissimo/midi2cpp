#include "test_common.h"
#include "midi2_device.h"

uint32_t g_captured_tx[CAPTURE_MAX] = {0};
size_t   g_captured_tx_len = 0;
uint32_t g_test_now_ms = 0;

using namespace midi2;

static void test_sendSysEx7_fragments_20_bytes(void) {
    TEST("sendSysEx7 with 20 bytes fragments into 4 UMPs (start+continue+continue+end)");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    uint8_t data[20];
    for (int i = 0; i < 20; ++i) data[i] = (uint8_t)i;

    bool ok = d.sendSysEx7(/*group*/ 0, data, 20);
    CHECK(ok, "sendSysEx7 returned false");
    // 20 bytes / 6 per UMP = 3 full UMPs + 1 partial (2 bytes) = 4 UMPs total
    // Each UMP = 2 words. Total = 8 words.
    CHECK_EQ(g_captured_tx_len, 8u, "expected 4 UMPs * 2 words = 8 words");
    // All UMPs are MT 0x3
    for (size_t i = 0; i < 8; i += 2) {
        uint32_t w0 = g_captured_tx[i];
        if (((w0 >> 28) & 0xFu) != 0x3u) {
            std::printf("FAIL: word %zu MT = 0x%X (expected 0x3)\n", i, (w0 >> 28) & 0xFu);
            g_failed++;
            return;
        }
    }
    PASS();
}

static void test_sendSysEx8_fragments_40_bytes(void) {
    TEST("sendSysEx8 with 40 bytes fragments into 4 UMPs (13+13+13+1)");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    uint8_t data[40];
    for (int i = 0; i < 40; ++i) data[i] = (uint8_t)i;

    bool ok = d.sendSysEx8(/*group*/ 0, /*streamId*/ 0x42, data, 40);
    CHECK(ok, "sendSysEx8 returned false");
    // 40 bytes / 13 per UMP = 3 full UMPs + 1 partial (1 byte) = 4 UMPs total
    // Each MT 0x5 UMP = 4 words. Total = 16 words.
    CHECK_EQ(g_captured_tx_len, 16u, "expected 4 UMPs * 4 words = 16 words");
    // All UMPs are MT 0x5
    for (size_t i = 0; i < 16; i += 4) {
        uint32_t w0 = g_captured_tx[i];
        if (((w0 >> 28) & 0xFu) != 0x5u) {
            std::printf("FAIL: word %zu MT = 0x%X (expected 0x5)\n", i, (w0 >> 28) & 0xFu);
            g_failed++;
            return;
        }
    }
    PASS();
}

static void test_sendDeviceIdentity(void) {
    TEST("sendDeviceIdentity packs mfrId[3] MSB-first into uint32_t");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    uint8_t mfrId[3] = {0x7D, 0x12, 0x34};
    bool ok = d.sendDeviceIdentity(mfrId, /*family*/ 0x0001,
                                   /*model*/ 0x0042, /*version*/ 0x00010000);
    CHECK(ok, "sendDeviceIdentity returned false");
    CHECK_EQ(g_captured_tx_len, 4u, "MT 0xF = 4 words");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu, 0xFu, "MT = 0xF");
    PASS();
}

// ==================== ByteStreamConverter (D.1 + D.2) ====================

static void test_bsc_note_on_emits_mt2(void) {
    TEST("ByteStreamConverter: 0x90 0x3C 0x64 -> MT 0x2 NoteOn");
    ByteStreamConverter conv(/*group*/ 0);
    bool fired = false;
    uint32_t captured = 0;
    conv.onUmp([&](const uint32_t* w, uint8_t n) {
        fired = true;
        if (n >= 1) captured = w[0];
    });

    CHECK(!conv.feed(0x90), "status byte alone not complete");
    CHECK(!conv.feed(0x3C), "data1 alone not complete");
    CHECK( conv.feed(0x64), "data2 completes the message");
    CHECK(fired, "user callback fired");
    CHECK_EQ((captured >> 28) & 0xFu,  0x2u,  "MT = 0x2 (MIDI 1.0 CV)");
    CHECK_EQ((captured >> 16) & 0xFFu, 0x90u, "status = 0x90 (NoteOn ch0)");
    CHECK_EQ((captured >> 8)  & 0x7Fu, 60u,   "note = 60");
    CHECK_EQ(captured         & 0x7Fu, 100u,  "velocity = 100");
    PASS();
}

static void test_bsc_running_status(void) {
    TEST("ByteStreamConverter: running status reuses prior status byte");
    ByteStreamConverter conv(/*group*/ 0);
    int fired_count = 0;
    conv.onUmp([&](const uint32_t* /*w*/, uint8_t /*n*/) { ++fired_count; });

    // Establish a NoteOn ch0 status, then send two consecutive notes via
    // running status (no status byte between them).
    conv.feed(0x90); conv.feed(60); conv.feed(100);   // first NoteOn
    conv.feed(62);   conv.feed(110);                  // running NoteOn
    conv.feed(64);   conv.feed(120);                  // running NoteOn
    CHECK_EQ(fired_count, 3, "three NoteOn UMPs emitted via running status");
    PASS();
}

static void test_bsc_sysex_complete(void) {
    TEST("ByteStreamConverter: 0xF0 0x7D 0x01 0x02 0xF7 -> MT 0x3 SysEx7");
    ByteStreamConverter conv(/*group*/ 0);
    int sysex_packets = 0;
    bool seen_mt3 = false;
    conv.onUmp([&](const uint32_t* w, uint8_t /*n*/) {
        if (((w[0] >> 28) & 0xFu) == 0x3u) {
            ++sysex_packets;
            seen_mt3 = true;
        }
    });
    conv.feed(0xF0);
    conv.feed(0x7D);
    conv.feed(0x01);
    conv.feed(0x02);
    conv.feed(0xF7);
    CHECK(seen_mt3, "MT 0x3 SysEx7 UMP emitted");
    CHECK(sysex_packets >= 1, "at least one SysEx packet");
    PASS();
}

static void test_bsc_setGroup_changes_emitted_group(void) {
    TEST("ByteStreamConverter::setGroup changes the group of emitted UMPs");
    ByteStreamConverter conv(/*group*/ 0);
    conv.setGroup(7);
    uint32_t captured = 0;
    conv.onUmp([&](const uint32_t* w, uint8_t n) {
        if (n >= 1) captured = w[0];
    });
    conv.feed(0x90); conv.feed(60); conv.feed(100);
    CHECK_EQ((captured >> 24) & 0xFu, 7u, "group nibble = 7");
    PASS();
}

static void test_bsc_reset_clears_running_status(void) {
    TEST("ByteStreamConverter::reset clears running status");
    ByteStreamConverter conv(/*group*/ 0);
    int fired = 0;
    conv.onUmp([&](const uint32_t*, uint8_t) { ++fired; });
    conv.feed(0x90); conv.feed(60); conv.feed(100);  // 1 NoteOn
    conv.reset();
    // Without status byte after reset, single data bytes don't form a message.
    CHECK(!conv.feed(60), "no status -> no message");
    CHECK(!conv.feed(100), "still no message without status");
    CHECK_EQ(fired, 1, "only the pre-reset NoteOn fired");
    PASS();
}

static void test_bsc_realtime_in_sysex_two_callbacks(void) {
    TEST("ByteStreamConverter: F8 inside SysEx -> two callbacks, wire order");
    ByteStreamConverter conv(/*group*/ 0);
    uint32_t first[2] = {0, 0};
    uint32_t words[8] = {0};
    int fired = 0;
    conv.onUmp([&](const uint32_t* w, uint8_t n) {
        if (fired < 8) words[fired] = w[0];
        if (fired == 0 && n >= 2) { first[0] = w[0]; first[1] = w[1]; }
        ++fired;
    });

    conv.feed(0xF0);
    conv.feed(0x0A); conv.feed(0x0B); conv.feed(0x0C);
    conv.feed(0x0D); conv.feed(0x0F);
    // Real-Time clock lands mid-SysEx: one feed() call, two messages.
    CHECK(conv.feed(0xF8), "feed reports messages ready");
    CHECK_EQ(fired, 2, "two callbacks from a single byte");
    CHECK_EQ(first[0], 0x30150A0Bu, "partial SysEx packet first (START, 5 bytes)");
    CHECK_EQ(first[1], 0x0C0D0F00u, "payload preserved");
    CHECK_EQ(words[1], 0x10F80000u, "clock second, keeping wire order");

    conv.feed(0xF7);
    CHECK_EQ(fired, 3, "SysEx end closes the message");
    PASS();
}

int main(void) {
    test_sendSysEx7_fragments_20_bytes();
    test_sendSysEx8_fragments_40_bytes();
    test_sendDeviceIdentity();
    test_bsc_note_on_emits_mt2();
    test_bsc_running_status();
    test_bsc_sysex_complete();
    test_bsc_setGroup_changes_emitted_group();
    test_bsc_reset_clears_running_status();
    test_bsc_realtime_in_sysex_two_callbacks();
    REPORT_AND_EXIT();
}

#include "test_common.h"
#include "midi2_device.h"

uint32_t g_captured_tx[CAPTURE_MAX] = {0};
size_t   g_captured_tx_len = 0;
uint32_t g_test_now_ms = 0;

using namespace midi2;

static void test_sendTempo(void) {
    TEST("sendTempo emits MT 0xD with 10ns-per-quarter in word1");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    // 120 BPM = 500000 microseconds per quarter = 50000000 ten-nanoseconds
    bool ok = d.sendTempo(/*group*/ 0, /*ten_ns_per_qn*/ 50000000u);
    CHECK(ok, "sendTempo returned false");
    CHECK_EQ(g_captured_tx_len, 4u, "MT 0xD = 4 words");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu, 0xDu, "MT = 0xD");
    CHECK_EQ(g_captured_tx[1], 50000000u, "tempo value in word 1");
    PASS();
}

static void test_sendTimeSignature(void) {
    TEST("sendTimeSignature emits MT 0xD with num/denom in word1");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendTimeSignature(/*group*/ 0, /*num*/ 4, /*denom*/ 4);
    CHECK(ok, "sendTimeSignature returned false");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu, 0xDu, "MT = 0xD");
    CHECK_EQ((g_captured_tx[1] >> 24) & 0xFFu, 4u, "numerator = 4");
    CHECK_EQ((g_captured_tx[1] >> 16) & 0xFFu, 4u, "denominator = 4");
    PASS();
}

static void test_sendKeySignature(void) {
    TEST("sendKeySignature with -3 flats minor");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendKeySignature(/*group*/ 0, /*sharpsFlats*/ -3, /*minor*/ true);
    CHECK(ok, "sendKeySignature returned false");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu, 0xDu, "MT = 0xD");
    // -3 in 4-bit signed = 0xD
    CHECK_EQ((g_captured_tx[1] >> 28) & 0xFu, 0xDu, "sharpsFlats = -3 (0xD in 4-bit)");
    CHECK_EQ((g_captured_tx[1] >> 22) & 0x3u, 1u,   "key_type = minor (1)");
    PASS();
}

static void test_sendChordName(void) {
    TEST("sendChordName unpacks 20-field struct into MT 0xD chord packet");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    ChordDescriptor c{};
    c.address       = 0;     // channel-addressable
    c.channel       = 5;
    c.tonicSharpFlat = 0;
    c.tonicNote     = 3;     // C
    c.chordType     = 0x01;  // major
    c.alt1Type = 0; c.alt1Degree = 0;
    c.alt2Type = 0; c.alt2Degree = 0;
    c.alt3Type = 0; c.alt3Degree = 0;
    c.alt4Type = 0; c.alt4Degree = 0;
    c.bassSharpFlat = 0;
    c.bassNote     = 0;
    c.bassChordType = 0;
    c.bassAlt1Type = 0; c.bassAlt1Degree = 0;
    c.bassAlt2Type = 0; c.bassAlt2Degree = 0;

    bool ok = d.sendChordName(/*group*/ 0, c);
    CHECK(ok, "sendChordName returned false");
    CHECK_EQ(g_captured_tx_len, 4u, "MT 0xD = 4 words");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu, 0xDu, "MT = 0xD");
    CHECK_EQ((g_captured_tx[1] >> 24) & 0xFu, 3u,   "tonicNote = 3 (C)");
    CHECK_EQ((g_captured_tx[1] >> 16) & 0xFFu, 0x01u, "chordType = major");
    PASS();
}

static void test_sendFlexText(void) {
    TEST("sendFlexText emits MT 0xD bank/status with up to 12 UTF-8 bytes");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    // bank 0x01 = metadata, status 0x04 = composer name
    bool ok = d.sendFlexText(/*group*/ 0, /*bank*/ 0x01, /*status*/ 0x04, "midi2cpp");
    CHECK(ok, "sendFlexText returned false");
    CHECK_EQ(g_captured_tx_len, 4u, "MT 0xD = 4 words");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu, 0xDu, "MT = 0xD");
    // bank in low 16 bits of word 0, exact layout depends on flex_w0_full
    PASS();
}

static void test_sendFlexText_rejects_long_string(void) {
    TEST("sendFlexText returns false for text > 12 bytes (no silent truncation)");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendFlexText(/*group*/ 0, /*bank*/ 0x01, /*status*/ 0x04,
                             "way too long for one ump");  // 24 bytes
    CHECK(!ok, "should have refused");
    CHECK_EQ(g_captured_tx_len, 0u, "no UMPs emitted on refusal");
    PASS();
}

static void test_sendFlexText_rejects_null(void) {
    TEST("sendFlexText returns false on null pointer");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendFlexText(/*group*/ 0, /*bank*/ 0x01, /*status*/ 0x04, nullptr);
    CHECK(!ok, "should have refused null");
    PASS();
}

int main(void) {
    test_sendTempo();
    test_sendTimeSignature();
    test_sendKeySignature();
    test_sendChordName();
    test_sendFlexText();
    test_sendFlexText_rejects_long_string();
    test_sendFlexText_rejects_null();
    REPORT_AND_EXIT();
}

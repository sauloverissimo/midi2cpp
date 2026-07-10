#include "test_common.h"
#include "midi2_device.h"

uint32_t g_captured_tx[CAPTURE_MAX] = {0};
size_t   g_captured_tx_len = 0;
uint32_t g_test_now_ms = 0;

using namespace midi2;


static void test_feedRx_slices_multi_packet_bursts(void) {
    TEST("feedRx slices a multi-packet burst per UMP packet (regression: paginated PE GET 404)");
    // midi2_proc_feed consumes exactly one packet per call. A USB FIFO
    // drain hands feedRx several packets back-to-back; before the fix the
    // wrapper passed the whole burst in one call and every packet after
    // the first was silently dropped, truncating multi-packet SysEx7 runs.
    Device d;
    d.setWriteFn(capture_write);
    d.setMounted(true);
    d.setAltSetting(1);
    d.begin();

    // Reassembled SysEx7 destination: count completed messages + bytes.
    static size_t   s_runs = 0;
    static uint16_t s_len  = 0;
    d.onSysEx7([](uint8_t, const uint8_t*, uint16_t len) {
        s_runs++;
        s_len = len;
    });
    s_runs = 0; s_len = 0;

    // 18 payload bytes -> 3 SysEx7 packets (START/CONTINUE/END), fed as ONE
    // burst of 6 words, exactly like an RxRing slot / FIFO drain would.
    const uint8_t payload[18] = {
        0x7E, 0x7F, 0x0D, 0x7F, 0x01, 0x02,
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
    };
    uint32_t burst[6];
    for (int p = 0; p < 3; ++p) {
        uint8_t status = (p == 0) ? 0x1u : (p == 2) ? 0x3u : 0x2u;
        const uint8_t* b = &payload[p * 6];
        burst[p*2]     = (0x3u << 28) | ((uint32_t)status << 20) | (6u << 16)
                       | ((uint32_t)b[0] << 8) | b[1];
        burst[p*2 + 1] = ((uint32_t)b[2] << 24) | ((uint32_t)b[3] << 16)
                       | ((uint32_t)b[4] << 8) | b[5];
    }
    d.feedRx(burst, 6);

    CHECK_EQ(s_runs, (size_t)1, "one complete SysEx7 run expected");
    CHECK_EQ(s_len, (uint16_t)18, "all 18 bytes must survive the burst");
    PASS();
}

static void test_device_defaults(void) {
    TEST("Device defaults to unmounted + alt 0");
    Device d;
    CHECK(!d.isMounted(), "expected unmounted");
    CHECK_EQ(d.altSetting(), 0u, "expected alt 0");
    PASS();
}

static void test_device_begin_no_crash(void) {
    TEST("Device::begin does not crash on host");
    Device d;
    d.begin();
    CHECK(!d.isMounted(), "still unmounted in host tests");
    CHECK_EQ(d.altSetting(), 0u, "alt still 0 (no USB enumeration on host)");
    PASS();
}

static void test_device_task_no_crash(void) {
    TEST("Device::task does not crash on host");
    Device d;
    d.begin();
    for (int i = 0; i < 10; ++i) d.task();
    PASS();
}

static void test_sendNoteOn_emits_mt4_0x90(void) {
    TEST("sendNoteOn emits MT 0x4 status 0x9 with 16-bit velocity");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendNoteOn(/*group*/ 0, /*ch*/ 3, /*note*/ 60, /*vel16*/ 20000);
    CHECK(ok, "sendNoteOn returned false");
    CHECK_EQ(g_captured_tx_len, 2u, "expected 2 words");

    uint32_t w0 = g_captured_tx[0];
    CHECK_EQ((w0 >> 28) & 0xFu,  0x4u, "MT = 0x4");
    CHECK_EQ((w0 >> 24) & 0xFu,  0x0u, "group = 0");
    CHECK_EQ((w0 >> 20) & 0xFu,  0x9u, "status hi nibble = 0x9");
    CHECK_EQ((w0 >> 16) & 0xFu,  0x3u, "channel = 3");
    CHECK_EQ((w0 >> 8)  & 0xFFu, 60u,  "note = 60");

    uint32_t w1 = g_captured_tx[1];
    CHECK_EQ(w1 >> 16, 20000u, "velocity16 = 20000");
    PASS();
}

static void test_sendNoteOff_emits_mt4_0x80(void) {
    TEST("sendNoteOff emits MT 0x4 status 0x8 with 16-bit velocity");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendNoteOff(/*group*/ 1, /*ch*/ 5, /*note*/ 64, /*vel16*/ 0x4000);
    CHECK(ok, "sendNoteOff returned false");
    CHECK_EQ(g_captured_tx_len, 2u, "expected 2 words");

    uint32_t w0 = g_captured_tx[0];
    CHECK_EQ((w0 >> 28) & 0xFu,  0x4u, "MT = 0x4");
    CHECK_EQ((w0 >> 24) & 0xFu,  0x1u, "group = 1");
    CHECK_EQ((w0 >> 20) & 0xFu,  0x8u, "status hi nibble = 0x8");
    CHECK_EQ((w0 >> 16) & 0xFu,  0x5u, "channel = 5");
    CHECK_EQ((w0 >> 8)  & 0xFFu, 64u,  "note = 64");

    uint32_t w1 = g_captured_tx[1];
    CHECK_EQ(w1 >> 16, 0x4000u, "velocity16 = 0x4000");
    PASS();
}

static void test_sendCC_emits_mt4_0xB(void) {
    TEST("sendCC emits MT 0x4 status 0xB with 32-bit value");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendCC(/*group*/ 2, /*ch*/ 7, /*idx*/ 0x40, /*val*/ 0xDEADBEEFu);
    CHECK(ok, "sendCC returned false");
    CHECK_EQ(g_captured_tx_len, 2u, "expected 2 words");
    CHECK_EQ((g_captured_tx[0] >> 20) & 0xFu,  0xBu, "status hi nibble = 0xB");
    CHECK_EQ((g_captured_tx[0] >> 16) & 0xFu,  0x7u, "channel = 7");
    CHECK_EQ((g_captured_tx[0] >> 8)  & 0xFFu, 0x40u, "index = 0x40");
    CHECK_EQ(g_captured_tx[1], 0xDEADBEEFu, "value");
    PASS();
}

static void test_sendProgram_with_bank(void) {
    TEST("sendProgram with bank emits bank_valid bit + MSB/LSB");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendProgram(/*group*/ 0, /*ch*/ 1, /*prog*/ 42,
                            /*bankMSB*/ 0x10, /*bankLSB*/ 0x20, /*bankValid*/ true);
    CHECK(ok, "sendProgram returned false");
    CHECK_EQ((g_captured_tx[0] >> 20) & 0xFu,  0xCu, "status hi nibble = 0xC");
    // word 0 byte 4 (option flags, bit 0 = Bank Valid) per M2-104 §7.4.9.
    CHECK_EQ(g_captured_tx[0] & 0x1u,          1u,    "bank_valid bit set in byte 4");
    CHECK_EQ(g_captured_tx[1] >> 31,           0u,    "byte 5 MSB reserved/zero");
    CHECK_EQ((g_captured_tx[1] >> 24) & 0x7Fu, 42u,   "program = 42");
    CHECK_EQ((g_captured_tx[1] >> 8)  & 0x7Fu, 0x10u, "bankMSB = 0x10");
    CHECK_EQ(g_captured_tx[1]         & 0x7Fu, 0x20u, "bankLSB = 0x20");
    PASS();
}

static void test_sendPitchBend_emits_mt4_0xE(void) {
    TEST("sendPitchBend emits MT 0x4 status 0xE with 32-bit value");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendPitchBend(/*group*/ 0, /*ch*/ 0, /*val*/ 0x80000000u);
    CHECK(ok, "sendPitchBend returned false");
    CHECK_EQ((g_captured_tx[0] >> 20) & 0xFu, 0xEu, "status hi nibble = 0xE");
    CHECK_EQ(g_captured_tx[1], 0x80000000u, "value (center)");
    PASS();
}

static void test_sendRpn(void) {
    TEST("sendRpn emits MT 0x4 status 0x2 with bank/index in word0 b3/b4");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendRpn(/*group*/ 0, /*ch*/ 0, /*msb*/ 0, /*lsb*/ 1, /*val*/ 0x12345678u);
    CHECK(ok, "sendRpn returned false");
    CHECK_EQ((g_captured_tx[0] >> 20) & 0xFu,  0x2u, "status hi nibble = 0x2 (RPN)");
    CHECK_EQ((g_captured_tx[0] >> 8)  & 0xFFu, 0u,   "msb = 0");
    CHECK_EQ(g_captured_tx[0]         & 0xFFu, 1u,   "lsb = 1");
    CHECK_EQ(g_captured_tx[1], 0x12345678u, "value");
    PASS();
}

static void test_sendPerNotePitchBend(void) {
    TEST("sendPerNotePitchBend emits MT 0x4 status 0x6");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendPerNotePitchBend(/*group*/ 0, /*ch*/ 0, /*note*/ 60, /*val*/ 0x80000000u);
    CHECK(ok, "sendPerNotePitchBend returned false");
    CHECK_EQ((g_captured_tx[0] >> 20) & 0xFu, 0x6u, "status hi nibble = 0x6");
    CHECK_EQ((g_captured_tx[0] >> 8) & 0xFFu, 60u, "note = 60");
    PASS();
}

static void test_sendPerNoteManagement(void) {
    TEST("sendPerNoteManagement emits MT 0x4 status 0xF + flags");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendPerNoteManagement(/*group*/ 0, /*ch*/ 0, /*note*/ 64,
                                      /*detach*/ true, /*reset*/ true);
    CHECK(ok, "sendPerNoteManagement returned false");
    CHECK_EQ((g_captured_tx[0] >> 20) & 0xFu, 0xFu, "status hi nibble = 0xF");
    CHECK_EQ(g_captured_tx[0]         & 0xFFu, 0x03u, "detach|reset flags = 0x03");
    PASS();
}

// ==================== MT 0x0 Utility ====================

static void test_sendJRTimestamp(void) {
    TEST("sendJRTimestamp emits MT 0x0 status 0x20");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendJRTimestamp(/*group*/ 0, /*ts*/ 0x1234);
    CHECK(ok, "sendJRTimestamp returned false");
    CHECK_EQ(g_captured_tx_len, 1u, "1 word");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu,  0x0u,   "MT = 0x0");
    CHECK_EQ((g_captured_tx[0] >> 20) & 0xFu,  0x2u,   "status nibble = 0x2 (JR Timestamp)");
    CHECK_EQ(g_captured_tx[0]         & 0xFFFFu, 0x1234u, "timestamp");
    PASS();
}

// ==================== MT 0x1 System ====================

static void test_sendClock(void) {
    TEST("sendClock emits MT 0x1 status 0xF8");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendClock(/*group*/ 0);
    CHECK(ok, "sendClock returned false");
    CHECK_EQ(g_captured_tx_len, 1u, "1 word");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu,  0x1u,  "MT = 0x1");
    CHECK_EQ((g_captured_tx[0] >> 16) & 0xFFu, 0xF8u, "status = 0xF8 (Timing Clock)");
    PASS();
}

static void test_sendSongPosition(void) {
    TEST("sendSongPosition emits MT 0x1 status 0xF2 with 14-bit split");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendSongPosition(/*group*/ 0, /*beats14*/ 0x1F2A);
    CHECK(ok, "sendSongPosition returned false");
    CHECK_EQ((g_captured_tx[0] >> 16) & 0xFFu, 0xF2u, "status = 0xF2");
    // 14-bit MIDI: data1 = LSB (0x2A & 0x7F), data2 = MSB ((0x1F2A >> 7) & 0x7F = 0x3E)
    CHECK_EQ((g_captured_tx[0] >> 8)  & 0x7Fu, 0x2Au, "data1 (LSB) = 0x2A");
    CHECK_EQ(g_captured_tx[0]         & 0x7Fu, 0x3Eu, "data2 (MSB) = 0x3E");
    PASS();
}

// ==================== MT 0x2 MIDI 1.0 ====================

static void test_sendNoteOn1(void) {
    TEST("sendNoteOn1 emits MT 0x2 status 0x90");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendNoteOn1(/*group*/ 0, /*ch*/ 5, /*note*/ 60, /*vel7*/ 100);
    CHECK(ok, "sendNoteOn1 returned false");
    CHECK_EQ(g_captured_tx_len, 1u, "1 word (MT 0x2 is 1 word)");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu,  0x2u,  "MT = 0x2");
    CHECK_EQ((g_captured_tx[0] >> 16) & 0xFFu, 0x95u, "status = 0x95 (NoteOn ch5)");
    CHECK_EQ((g_captured_tx[0] >> 8)  & 0x7Fu, 60u,   "note = 60");
    CHECK_EQ(g_captured_tx[0]         & 0x7Fu, 100u,  "velocity = 100");
    PASS();
}

static void test_sendCC1(void) {
    TEST("sendCC1 emits MT 0x2 status 0xB0");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendCC1(/*group*/ 1, /*ch*/ 0, /*idx*/ 7, /*val*/ 64);
    CHECK(ok, "sendCC1 returned false");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu,  0x2u,  "MT = 0x2");
    CHECK_EQ((g_captured_tx[0] >> 24) & 0xFu,  0x1u,  "group = 1");
    CHECK_EQ((g_captured_tx[0] >> 16) & 0xFFu, 0xB0u, "status = 0xB0 (CC ch0)");
    PASS();
}

// ==================== Inbound dispatch ====================
// Injects synthesized UMP words via midi2_proc_feed and verifies the C++
// std::function fires through the C99 trampoline. Exercises the full
// pipeline: proc -> dispatch -> tramp_* -> DeviceState::cb_* -> user lambda.

static void test_onNoteOn_fires_on_inbound_mt4(void) {
    TEST("onNoteOn fires for inbound MT 0x4 (proc_feed -> dispatch -> trampoline)");
    Device d;
    d.begin();

    uint8_t  captured_ch = 0xFF;
    uint8_t  captured_note = 0;
    uint16_t captured_vel = 0;
    d.onNoteOn([&](uint8_t /*g*/, uint8_t ch, uint8_t n, uint16_t v,
                   uint8_t /*at*/, uint16_t /*ad*/) {
        captured_ch = ch;
        captured_note = n;
        captured_vel = v;
    });

    uint32_t words[2];
    midi2_msg_note_on(words, /*group*/ 0, /*ch*/ 7, /*note*/ 60, /*vel*/ 0x8000,
                      /*attr_type*/ 0, /*attr_data*/ 0);
    midi2_proc_feed(d.procState(), words, 2);

    CHECK_EQ(captured_ch,   7u,      "channel");
    CHECK_EQ(captured_note, 60u,     "note");
    CHECK_EQ(captured_vel,  0x8000u, "velocity");
    PASS();
}

static void test_onCC_fires_on_inbound_mt4(void) {
    TEST("onCC fires for inbound MT 0x4 CC");
    Device d;
    d.begin();

    uint32_t captured_val = 0;
    d.onCC([&](uint8_t /*g*/, uint8_t /*ch*/, uint8_t /*idx*/, uint32_t v) {
        captured_val = v;
    });

    uint32_t words[2];
    midi2_msg_cc(words, 0, 0, 7, 0xCAFEBABEu);
    midi2_proc_feed(d.procState(), words, 2);
    CHECK_EQ(captured_val, 0xCAFEBABEu, "32-bit value");
    PASS();
}

static void test_setUpscaleMt2_routes_mt2_to_mt4_callback(void) {
    TEST("setUpscaleMt2(true): inbound MT 0x2 NoteOn fires onNoteOn (MT 0x4)");
    Device d;
    d.begin();
    d.setUpscaleMt2(true);

    uint16_t captured_vel16 = 0;
    bool mt4_fired = false;
    bool mt2_fired = false;
    d.onNoteOn([&](uint8_t, uint8_t, uint8_t, uint16_t v, uint8_t, uint16_t) {
        mt4_fired = true;
        captured_vel16 = v;
    });
    d.onNoteOn1([&](uint8_t, uint8_t, uint8_t, uint8_t) { mt2_fired = true; });

    // Build MIDI 1.0 NoteOn (status 0x90, ch=0, note=60, vel=0x40).
    uint32_t w = midi2_msg_from_midi1(0, 0x90, 60, 0x40);
    midi2_proc_feed(d.procState(), &w, 1);

    CHECK(mt4_fired,  "onNoteOn (MT 0x4) should fire under upscale");
    CHECK(!mt2_fired, "onNoteOn1 (MT 0x2) should NOT fire under upscale");
    // 7-bit 0x40 bit-replicated -> 16-bit ~ 0x8080
    CHECK(captured_vel16 != 0, "velocity scaled to 16-bit non-zero");
    PASS();
}

// ==================== Realistic flow: begin() then send ====================

static void test_begin_then_send_realistic_flow(void) {
    TEST("begin() before send works (realistic Arduino setup/loop flow)");
    Device d;
    d.setWriteFn(capture_write);
    d.begin();  // initializes proc + dispatch state
    capture_reset();

    bool ok = d.sendNoteOn(/*group*/ 0, /*ch*/ 0, /*note*/ 60, /*vel16*/ 0x8000);
    CHECK(ok, "sendNoteOn after begin returned false");
    CHECK_EQ(g_captured_tx_len, 2u, "2 words emitted after begin()");
    PASS();
}

// ==================== MT 0x4 attribute round-trip ====================

static void test_sendNoteOn_attrData_full_16bit(void) {
    TEST("sendNoteOn round-trips attr_data full 16 bits (I-1)");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    // Pitch7_9 attribute (type 0x03) carries a 16-bit attribute_data.
    bool ok = d.sendNoteOn(/*group*/ 0, /*ch*/ 0, /*note*/ 60, /*vel16*/ 0x8000,
                           /*attrType*/ 0x03, /*attrData*/ 0x1234);
    CHECK(ok, "sendNoteOn returned true");
    CHECK_EQ(g_captured_tx_len, 2u, "2-word UMP emitted");
    CHECK_EQ(g_captured_tx[0] & 0xFFu,      0x03u,   "attr_type=0x03 in byte 4");
    CHECK_EQ(g_captured_tx[1] & 0xFFFFu,    0x1234u, "attr_data=0x1234 in w[1] low 16 bits");
    PASS();
}

// ==================== MT 0x1 system generic (I-5) ====================

static void test_sendSystemGeneric(void) {
    TEST("sendSystemGeneric emits arbitrary MT 0x1 status + data bytes");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendSystemGeneric(/*group*/ 1, /*status*/ 0xF1,
                                  /*data1*/ 0x40, /*data2*/ 0x00);
    CHECK(ok, "sendSystemGeneric returned false");
    CHECK_EQ(g_captured_tx_len, 1u, "1 word");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu,  0x1u,  "MT = 0x1");
    CHECK_EQ((g_captured_tx[0] >> 24) & 0xFu,  0x1u,  "group = 1");
    CHECK_EQ((g_captured_tx[0] >> 16) & 0xFFu, 0xF1u, "status = 0xF1");
    CHECK_EQ((g_captured_tx[0] >> 8)  & 0x7Fu, 0x40u, "data1 = 0x40");
    PASS();
}

// ==================== MT 0xF UMP Stream ====================

static void test_sendStartOfClip(void) {
    TEST("sendStartOfClip emits MT 0xF status 0x020 (endpoint-wide, no group)");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendStartOfClip();
    CHECK(ok, "sendStartOfClip returned false");
    CHECK_EQ(g_captured_tx_len, 4u, "MT 0xF = 4 words");
    uint32_t w0 = g_captured_tx[0];
    CHECK_EQ((w0 >> 28) & 0xFu, 0xFu, "MT = 0xF");
    CHECK_EQ((w0 >> 26) & 0x03u, 0u, "format = 0 (complete)");
    CHECK_EQ((w0 >> 16) & 0x3FFu, 0x020u, "status = 0x020 (Start of Clip)");
    PASS();
}

static void test_sendEndOfClip(void) {
    TEST("sendEndOfClip emits MT 0xF status 0x021 (endpoint-wide)");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendEndOfClip();
    CHECK(ok, "sendEndOfClip returned false");
    CHECK_EQ(g_captured_tx_len, 4u, "MT 0xF = 4 words");
    uint32_t w0 = g_captured_tx[0];
    CHECK_EQ((w0 >> 28) & 0xFu, 0xFu, "MT = 0xF");
    CHECK_EQ((w0 >> 16) & 0x3FFu, 0x021u, "status = 0x021 (End of Clip)");
    PASS();
}

// ==================== JR heartbeat timer ====================

static void test_jr_heartbeat_emits_on_interval(void) {
    TEST("enableJRHeartbeat emits MT 0x0 status 0x2 every interval ms");
    Device d;
    d.begin();
    d.setWriteFn(capture_write);

    d.enableJRHeartbeat(500);  // emit every 500 ms

    // t = 0: first task() triggers heartbeat (jr_last_heartbeat_ms == 0).
    capture_reset();
    d.setNowFn(test_now_fn); test_set_now(0);
    d.task();
    CHECK_EQ(g_captured_tx_len, 1u, "first heartbeat at t=0");
    CHECK_EQ((g_captured_tx[0] >> 28) & 0xFu, 0x0u, "MT = 0x0 utility");
    CHECK_EQ((g_captured_tx[0] >> 20) & 0xFu, 0x2u, "status = 0x2 (JR Timestamp)");

    // t = 250: still inside the interval, no heartbeat.
    capture_reset();
    d.setNowFn(test_now_fn); test_set_now(250);
    d.task();
    CHECK_EQ(g_captured_tx_len, 0u, "no heartbeat before interval elapses");

    // t = 600: interval elapsed, second heartbeat fires.
    capture_reset();
    d.setNowFn(test_now_fn); test_set_now(600);
    d.task();
    CHECK_EQ(g_captured_tx_len, 1u, "second heartbeat after 500 ms");
    PASS();
}

// ==================== Field-tested helpers ====================

static void test_setUmpGroup_rewrites_word0(void) {
    TEST("setUmpGroup rewrites the group nibble of an MT 0x4 word");
    uint32_t words[2];
    midi2_msg_note_on(words, /*group*/ 0, 0, 60, 0x8000, 0, 0);
    Device::setUmpGroup(&words[0], 7);
    CHECK_EQ((words[0] >> 24) & 0xFu, 7u, "group set to 7");
    PASS();
}

static void test_downgradeMt4ToMt2_translates_note_on(void) {
    TEST("downgradeMt4ToMt2 translates MT 0x4 NoteOn to MT 0x2 NoteOn");
    uint32_t in[2];
    midi2_msg_note_on(in, /*group*/ 0, /*ch*/ 3, /*note*/ 60, /*vel*/ 0xFFFF, 0, 0);
    uint32_t out[1] = {0};
    uint8_t outCount = 0;
    bool ok = Device::downgradeMt4ToMt2(in, /*count*/ 2, out, &outCount);
    CHECK(ok, "downgrade returned false");
    CHECK_EQ(outCount, 1u, "1 MT 0x2 word emitted");
    CHECK_EQ((out[0] >> 28) & 0xFu,  0x2u,  "MT = 0x2");
    CHECK_EQ((out[0] >> 16) & 0xFFu, 0x93u, "status = 0x93 (NoteOn ch3)");
    CHECK_EQ((out[0] >> 8)  & 0x7Fu, 60u,   "note");
    PASS();
}

static void test_cableEventToUmp_translates_note_on(void) {
    TEST("cableEventToUmp converts USB MIDI 1.0 cable event to MT 0x2 UMP");
    // USB MIDI 1.0 cable event: byte0 = (cable<<4)|CIN, then 3 status/data
    // bytes. Note On has CIN 0x9: byte0 = 0x09 (cable 0, CIN 9).
    uint32_t cable_event = 0x643C9009u;  // little-endian: 0x09, 0x90, 0x3C, 0x64
    uint32_t ump_out = 0;
    bool ok = Device::cableEventToUmp(cable_event, /*group*/ 0, &ump_out);
    CHECK(ok, "cableEventToUmp returned false");
    CHECK_EQ((ump_out >> 28) & 0xFu, 0x2u, "MT = 0x2 (MIDI 1.0 CV)");
    CHECK_EQ((ump_out >> 16) & 0xFFu, 0x90u, "status = 0x90 (NoteOn ch0)");
    CHECK_EQ((ump_out >> 8)  & 0x7Fu, 0x3Cu, "note = 0x3C (60)");
    CHECK_EQ(ump_out         & 0x7Fu, 0x64u, "velocity = 0x64 (100)");
    PASS();
}

static void test_setGroupRemap_writes_proc_table(void) {
    TEST("setGroupRemap copies the 16-byte map into the proc state");
    Device d;
    d.begin();

    uint8_t map[16];
    for (int i = 0; i < 16; ++i) map[i] = (uint8_t)((15 - i) & 0x0F);  // reverse
    d.setGroupRemap(map);

    midi2_proc_state* proc = d.procState();
    for (int i = 0; i < 16; ++i) {
        if (proc->group_map[i] != ((15 - i) & 0x0F)) {
            std::printf("FAIL: slot %d = %u, want %u\n",
                        i, proc->group_map[i], (15 - i) & 0x0F);
            g_failed++;
            return;
        }
    }
    PASS();
}

static void test_sendDeviceIdentity_rejects_null(void) {
    TEST("sendDeviceIdentity returns false on null mfrId pointer");
    Device d;
    d.setWriteFn(capture_write);
    capture_reset();

    bool ok = d.sendDeviceIdentity(/*mfrId*/ nullptr, 0, 0, 0);
    CHECK(!ok, "should refuse null");
    CHECK_EQ(g_captured_tx_len, 0u, "no UMP emitted");
    PASS();
}

static void test_reassembled_sysex7_two_hop_context(void) {
    TEST("onSysEx7 fires after multi-UMP fragment reassembly (proc -> dispatch.context)");
    Device d;
    d.begin();

    // Capture the reassembled bytes plus group.
    uint8_t  captured_data[64] = {0};
    uint16_t captured_len = 0;
    uint8_t  captured_group = 0xFF;
    d.onSysEx7([&](uint8_t g, const uint8_t* data, uint16_t len) {
        captured_group = g;
        captured_len = len;
        for (uint16_t i = 0; i < len && i < sizeof(captured_data); ++i) {
            captured_data[i] = data[i];
        }
    });

    // Build a 14-byte SysEx7 payload spread across 3 MT 0x3 UMPs:
    //   UMP1 (start)    = 6 bytes
    //   UMP2 (continue) = 6 bytes
    //   UMP3 (end)      = 2 bytes
    uint8_t payload[14];
    for (int i = 0; i < 14; ++i) payload[i] = (uint8_t)(0x10 + i);

    midi2_proc_send_sysex7(/*group*/ 5, payload, 14,
        [](const uint32_t* w, uint32_t n, void* ctx) -> uint32_t {
            // Funnel the fragmenter's output straight back into proc_feed
            // to simulate the loopback path the real RX side would take.
            auto* proc = static_cast<midi2_proc_state*>(ctx);
            midi2_proc_feed(proc, w, (uint8_t)n);
            return n;
        }, d.procState());

    CHECK_EQ((unsigned)captured_group, 5u,  "group preserved through reassembly");
    CHECK_EQ((unsigned)captured_len,   14u, "length matches input");
    for (int i = 0; i < 14; ++i) {
        if (captured_data[i] != (uint8_t)(0x10 + i)) {
            std::printf("FAIL: byte %d = 0x%02X, want 0x%02X\n",
                        i, captured_data[i], 0x10 + i);
            g_failed++;
            return;
        }
    }
    PASS();
}

static void test_setNowFn_isolated(void) {
    TEST("setNowFn injects clock observable via heartbeat firing");
    Device d;
    d.begin();
    d.setWriteFn(capture_write);
    d.enableJRHeartbeat(100);

    // Drive one tick at t=0 to consume the initial-fire flag.
    capture_reset();
    d.setNowFn(test_now_fn); test_set_now(0);
    d.task();
    CHECK_EQ(g_captured_tx_len, 1u, "first heartbeat at t=0");

    // Advance clock by 50ms — below interval, no fire.
    capture_reset();
    d.setNowFn(test_now_fn); test_set_now(50);
    d.task();
    CHECK_EQ(g_captured_tx_len, 0u, "no fire at t=50 (interval=100)");

    // Reach the interval — fire.
    capture_reset();
    d.setNowFn(test_now_fn); test_set_now(100);
    d.task();
    CHECK_EQ(g_captured_tx_len, 1u, "fire at t=100");
    PASS();
}

int main(void) {
    test_device_defaults();
    test_device_begin_no_crash();
    test_device_task_no_crash();

    // MT 0x4
    test_sendNoteOn_emits_mt4_0x90();
    test_sendNoteOff_emits_mt4_0x80();
    test_sendCC_emits_mt4_0xB();
    test_sendProgram_with_bank();
    test_sendPitchBend_emits_mt4_0xE();
    test_sendRpn();
    test_sendPerNotePitchBend();
    test_sendPerNoteManagement();

    // MT 0x0
    test_sendJRTimestamp();

    // MT 0x1
    test_sendClock();
    test_sendSongPosition();

    // MT 0x2
    test_sendNoteOn1();
    test_sendCC1();

    // MT 0xF
    test_sendStartOfClip();
    test_sendEndOfClip();

    // Realistic flow + boundary cases
    test_begin_then_send_realistic_flow();
    test_sendNoteOn_attrData_full_16bit();
    test_sendSystemGeneric();

    // Inbound dispatch
    test_onNoteOn_fires_on_inbound_mt4();
    test_onCC_fires_on_inbound_mt4();
    test_setUpscaleMt2_routes_mt2_to_mt4_callback();

    // Heartbeat + helpers
    test_jr_heartbeat_emits_on_interval();
    test_setUmpGroup_rewrites_word0();
    test_downgradeMt4ToMt2_translates_note_on();
    test_cableEventToUmp_translates_note_on();

    // Coverage gaps + carry-over fixes
    test_feedRx_slices_multi_packet_bursts();
    test_setGroupRemap_writes_proc_table();
    test_sendDeviceIdentity_rejects_null();
    test_reassembled_sysex7_two_hop_context();
    test_setNowFn_isolated();

    REPORT_AND_EXIT();
}

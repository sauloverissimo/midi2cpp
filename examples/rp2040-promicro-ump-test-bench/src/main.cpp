/*
 * main.cpp, rp2040-promicro-ump-test-bench-showcase.
 *
 * Headless USB MIDI 2.0 device that emits a deterministic catalog of
 * 101 UMPs for the Windows MIDI Services consumer side. Three operating
 * modes coexist:
 *
 *   1. Boot auto-emit (§4.1). After enumeration, the device emits
 *      every catalog entry in order with a 50 ms inter-message pause.
 *      Captured via `midi endpoint <id> monitor -c capture.txt -n`.
 *
 *   2. NoteOn trigger (§4.2). At any time, an inbound MIDI 2.0 Note On
 *      on group=15 channel=0 selects catalog index = noteNumber and
 *      emits that single entry. Velocity ignored.
 *
 *   3. CC loop control (§4.3). Inbound MIDI 2.0 CC controller=120 on
 *      group=15 channel=0, with data interpreted as a catalog index,
 *      starts a re-emission loop at 50 ms intervals. CC controller=121
 *      on the same group/channel stops the loop.
 *
 * Identity, per spec doc §3:
 *
 *   USB VID:PID            0xCAFE:0x4078
 *   USB Manufacturer       "MIDI 2.0 Test Bench"
 *   USB Product            "UMP Reference Emitter"
 *   UMP Endpoint Name      "UMP Reference Emitter"
 *   UMP Product Instance   "UMPReferenceEmitter-bench-0001"
 *   Function Block Name    "Test Bench Group 0"
 *
 * EMIT log on UART GP0 / GP1 @ 115200 8N1. Read with any USB-Serial
 * adapter (PL2303, CP2102, FT232) tied to the corresponding pads on
 * the Pro Micro side.
 */
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "bsp/board_api.h"

#include "rp2040_midi2.h"
#include "catalog.h"
#if BENCH_AUTOFLOOD
#include "gabarito_battery.h"   // gab::kBattery: the musical interpretation battery
#endif

using namespace midi2;

/*--------------------------------------------------------------------+
 * Identity
 *--------------------------------------------------------------------*/
static const uint8_t  kMfrId[3]      = {0x7D, 0x00, 0x00};  // educational prefix
static const uint16_t kFamilyId      = 0x0001;
static const uint16_t kModelId       = 0x0001;
static const uint32_t kVersion       = 0x00010000;

/*--------------------------------------------------------------------+
 * Bench state
 *
 * The bench is a continuous emitter: the catalog cycles 0..100..0..
 * forever while mounted. The operator can open the Windows MIDI
 * Services monitor at any time and the next catalog pass will be
 * captured. NoteOn group=15 still fires a one-shot interleaved with
 * the cycle; CC 120 idx pauses the cycle and re-emits a single index
 * every kLoopPeriodMs; CC 121 resumes the cycle.
 *--------------------------------------------------------------------*/
struct BenchState {
    uint8_t  cycle_idx       = 0;        // next entry the cycle will emit
    uint32_t cycle_last_ms   = 0;        // last cycle-step timestamp
    bool     loop_active     = false;    // CC 120 pause-and-loop mode
    uint8_t  loop_idx        = 0;
    uint32_t loop_last_ms    = 0;
};

constexpr uint32_t kCycleStepMs  = 50;   // gap between consecutive catalog entries
constexpr uint32_t kLoopPeriodMs = 50;   // gap between re-emits in CC 120 loop mode

// Inbound triggers are captured in flags consumed by the main loop, so
// the emit work runs outside the USB dispatch context.
static volatile uint8_t g_pending_noteon_idx = 0xFF;
static BenchState       g_state{};

/*--------------------------------------------------------------------+
 * UMP Stream Discovery is answered by the TinyUSB built-in responder
 * (PR #3738): Endpoint Name from CFG_TUD_MIDI2_EP_NAME, FB direction +
 * group span from tud_midi2_gtb_desc_cb, FB name from tud_midi2_fb_name_cb
 * (in rp2040_midi2.cpp). No app-side stream responder is installed; Device
 * Identity is carried by MIDI-CI Discovery (SysEx) via ci.begin.
 *--------------------------------------------------------------------*/

/*--------------------------------------------------------------------+
 * Trigger handlers (§4.2 NoteOn, §4.3 CC)
 *--------------------------------------------------------------------*/
static void install_triggers(m2device& midi) {
    // §4.2 trigger: MIDI 2.0 Note On on group 15 channel 0. The verbose
    // onNoteOn callback exposes group + attribute fields; we filter on
    // group + channel and use noteNumber as the catalog index.
    midi.onNoteOn([](uint8_t group, uint8_t channel,
                     uint8_t note, uint16_t /*vel16*/,
                     uint8_t /*attrType*/, uint16_t /*attrData*/) {
        if (group != 15 || channel != 0) return;
        g_pending_noteon_idx = note;
        std::printf("[trigger] NoteOn idx=%u queued\r\n", (unsigned)note);
    });

    // §4.3 loop control: CC 120 start (top byte = catalog index), CC
    // 121 stop. Same group + channel filter as §4.2.
    midi.onCC([](uint8_t group, uint8_t channel,
                 uint8_t cc, uint32_t data32) {
        if (group != 15 || channel != 0) return;
        if (cc == 120) {
            g_state.loop_idx     = (uint8_t)(data32 >> 24);
            g_state.loop_active  = true;
            g_state.loop_last_ms = 0;
            std::printf("[trigger] loop START idx=%u\r\n",
                        (unsigned)g_state.loop_idx);
        } else if (cc == 121) {
            g_state.loop_active = false;
            std::printf("[trigger] loop STOP (was idx=%u)\r\n",
                        (unsigned)g_state.loop_idx);
        }
    });
}

#if BENCH_AUTOFLOOD
/*--------------------------------------------------------------------+
 * Flow validation emitter (BENCH_AUTOFLOOD).
 *
 * Two phases, looped: (1) a max-rate throughput burst so a receiver can prove
 * zero loss, and (2) the musical interpretation battery so the flow has real
 * note/chord gestures to index, pair, time, and name. Every note-on carries a
 * monotonic 16-bit sequence in its velocity.
 *--------------------------------------------------------------------*/
static constexpr uint8_t kBG = 1, kBCh = 0;
static uint32_t g_seq = 0;

static inline uint32_t af_now() { return (uint32_t)(time_us_64() / 1000ULL); }
static void af_pump(midi2::m2device& m) { rp2040_midi2::task(m); }

static void af_hold(midi2::m2device& m, uint32_t ms) {
    const uint32_t t0 = af_now();
    while (af_now() - t0 < ms) af_pump(m);
}

static void af_on(midi2::m2device& m, uint8_t n)  { m.sendNoteOn (kBG, kBCh, n, (uint16_t)g_seq); ++g_seq; }
static void af_off(midi2::m2device& m, uint8_t n) { m.sendNoteOff(kBG, kBCh, n, 0); }

// Phase 1: rapid on/off pairs at the line rate (nothing left held). The
// receiver's note-on sequence proves zero loss under a real burst.
static void af_throughput_burst(midi2::m2device& m, uint32_t pairs) {
    for (uint32_t i = 0; i < pairs; ++i) {
        const uint8_t n = (uint8_t)(48 + (i % 24));
        af_on(m, n);
        af_off(m, n);
    }
}

// Phase 2: the paced musical battery (triads, 7ths, inversions, open voicing,
// rolled chord, arpeggio, I-IV-V-I, hung note). The flow builds note/chord
// indices, durations, and names from these - the processing test, live.
static void af_run_battery(midi2::m2device& m) {
    for (unsigned i = 0; i < gab::kBatteryCount; ++i) {
        const gab::Challenge& c = gab::kBattery[i];
        uint16_t last = 0;
        gab::schedule(c, [&](uint16_t t, uint8_t note, bool on) {
            if (t > last) { af_hold(m, (uint32_t)(t - last)); last = t; }
            if (on) af_on(m, note); else af_off(m, note);
        });
        af_hold(m, c.gapMs);
    }
}
#endif  // BENCH_AUTOFLOOD

/*--------------------------------------------------------------------+
 * Main
 *--------------------------------------------------------------------*/
int main() {
    stdio_init_all();
    sleep_ms(200);

    std::printf("\r\n==================================================\r\n");
    std::printf("  rp2040-promicro-ump-test-bench  (VID:PID 0xCAFE:0x4078)\r\n");
    std::printf("  Manufacturer  : MIDI 2.0 Test Bench\r\n");
    std::printf("  Product       : UMP Reference Emitter\r\n");
    std::printf("  Catalog size  : %u entries (0..%u)\r\n",
                (unsigned)ump_test_bench::kCatalogSize,
                (unsigned)(ump_test_bench::kCatalogSize - 1));
    std::printf("==================================================\r\n");

    m2device midi;
    m2ci     ci(midi);

    rp2040_midi2::init(midi, ci);
    midi.begin();
    midi.enableJRHeartbeat(500);
    ci.begin(kMfrId, kFamilyId, kModelId, kVersion);

    install_triggers(midi);

    std::printf("[ready] entering main loop, waiting for mount + alt=1\r\n");

    while (true) {
        rp2040_midi2::task(midi);
        board_led_write(midi.isMounted());

        const bool     ready = midi.isMounted() && midi.altSetting() == 1;
        const uint32_t now   = (uint32_t)(time_us_64() / 1000ULL);

#if BENCH_AUTOFLOOD
        (void)now;
        // Loop both phases with a gap between so the flow's chord cluster
        // resets: throughput burst (zero-loss proof) then the musical battery
        // (note/chord index + duration + naming validation).
        if (ready) {
            af_throughput_burst(midi, 2000);
            af_hold(midi, 400);
            af_run_battery(midi);
            af_hold(midi, 400);
            std::printf("[bench] cycle done, seq=%lu\r\n", (unsigned long)g_seq);
        }
#else
        // §4.1 continuous catalog cycle. Default mode: 0..100..0..,
        // one entry every kCycleStepMs. Paused while CC 120 loop mode
        // is active (resumed by CC 121).
        if (ready && !g_state.loop_active) {
            if (g_state.cycle_last_ms == 0 ||
                (now - g_state.cycle_last_ms) >= kCycleStepMs) {
                ump_test_bench::catalogEmit(g_state.cycle_idx, midi);
                g_state.cycle_last_ms = now;
                g_state.cycle_idx     = (uint8_t)((g_state.cycle_idx + 1)
                                                  % ump_test_bench::kCatalogSize);
            }
        }

        // §4.2 NoteOn-driven on-demand emit. Fires once, immediately,
        // alongside whatever the cycle was about to do; useful for
        // pinpoint testing of a single index without stopping the cycle.
        if (ready && g_pending_noteon_idx != 0xFF) {
            uint8_t idx = g_pending_noteon_idx;
            g_pending_noteon_idx = 0xFF;
            ump_test_bench::catalogEmit(idx, midi);
        }

        // §4.3 CC-driven single-index loop. While active, the cycle is
        // paused and only loop_idx is re-emitted at kLoopPeriodMs.
        if (ready && g_state.loop_active) {
            if (g_state.loop_last_ms == 0 ||
                (now - g_state.loop_last_ms) >= kLoopPeriodMs) {
                ump_test_bench::catalogEmit(g_state.loop_idx, midi);
                g_state.loop_last_ms = now;
            }
        }
#endif  // BENCH_AUTOFLOOD
    }
}

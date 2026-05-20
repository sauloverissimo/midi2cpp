/*
 * feather_host.cpp: Adafruit Feather RP2040 USB Host platform glue.
 *
 * Owns: Pico SDK board init, PIO-USB host bring-up on GP16/GP17, USB-A
 * 5V power gate (GP18), TinyUSB host init (rhport 1), and the wiring
 * between TinyUSB host callbacks and the m2host five public hooks.
 * The application layer never sees any of this, it only sees the
 * midi2::m2host instance after init() returns.
 */
#include "feather_host.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/rand.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "class/midi/midi2_host.h"

// Hot-swap watchdog window. After all devices unmount, TinyUSB host on
// RP2040 occasionally fails to re-enumerate the next plug. If we
// observe "no devices for N ms" with N > 0 we reset the host stack
// (tuh_deinit + tusb_init). Set to 0 at compile time to disable.
#ifndef MIDI2CPP_HOST_WATCHDOG_MS
#define MIDI2CPP_HOST_WATCHDOG_MS 3000
#endif

namespace feather_host {

// We hold the m2host reference for the duration of the program so the
// TinyUSB host callbacks (free C functions, no closure) can route into
// the right object. Set in init(); the program is the lifetime. Lives
// at namespace scope (not anonymous) so the extern "C" callbacks below
// can address it as feather_host::g_midi.
midi2::m2host* g_midi = nullptr;

// bcdMSC is delivered to descriptor_cb but mount_cb only carries the
// alt-setting. Stash per-idx so the mount handler can pass the right
// value to m2host. Default 0x0200 covers spec v2 if descriptor_cb is
// somehow skipped.
uint16_t g_bcdMSC[midi2::Host::MAX_DEVICES] = {0x0200, 0x0200, 0x0200, 0x0200};

namespace {

// Outbound UMP, invoked by m2host for every sendXxx and the JR
// heartbeat injection. Forwards to TinyUSB MIDI 2.0 host stream write
// for the addressed device idx.
//
// We flush after every write for predictable latency. Auto-discovery
// on mount fires three small SysEx7 inquiries back-to-back so we pay
// three URBs there; for steady-state Channel Voice traffic the flush
// matches the message rate and keeps roundtrip times tight.
void platform_write_fn(uint8_t idx, const uint32_t* words, size_t count) {
    if (!tuh_midi2_mounted(idx)) return;
    tuh_midi2_ump_write(idx, words, (uint32_t)count);
    tuh_midi2_write_flush(idx);
}

// Monotonic millisecond clock used by m2host for CI Discovery timeout.
uint32_t platform_now_fn() {
    return (uint32_t)(time_us_64() / 1000ULL);
}

// Entropy source for host's own MUID (CI Initiator role).
uint32_t platform_rng_fn() {
    return get_rand_32();
}

#if MIDI2CPP_HOST_WATCHDOG_MS > 0
// Watchdog state, only walked from feather_host::task (single-threaded
// main loop), so plain globals are safe.
bool     g_had_device      = false;
uint32_t g_devices_lost_ms = 0;

void watchdog_tick(uint32_t now_ms) {
    bool any = false;
    for (uint8_t i = 0; i < midi2::Host::MAX_DEVICES; ++i) {
        if (tuh_midi2_mounted(i)) { any = true; break; }
    }
    if (any) {
        g_had_device      = true;
        g_devices_lost_ms = 0;
        return;
    }
    if (!g_had_device) return;
    if (g_devices_lost_ms == 0) {
        g_devices_lost_ms = now_ms;
        return;
    }
    if (now_ms - g_devices_lost_ms < MIDI2CPP_HOST_WATCHDOG_MS) return;

    // Stack reset. tuh_deinit drops endpoints + frees the device pool;
    // tusb_init rebuilds it. PIO-USB state machines are restarted by
    // the host driver as part of init.
    tuh_deinit(BOARD_TUH_RHPORT);
    tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUH_RHPORT, &host_init);
    g_had_device      = false;
    g_devices_lost_ms = 0;
}
#endif

}  // namespace

void power_on_usb_a() {
    // Drive 5V high on the USB-A connector. Idempotent, calling
    // multiple times is harmless. Must be called before tusb_init so
    // the upstream device has time to enumerate.
    gpio_init(18);
    gpio_set_dir(18, GPIO_OUT);
    gpio_put(18, 1);
}

void init(midi2::m2host& midi) {
    g_midi = &midi;

    // Pico SDK board bring-up. power_on_usb_a should already have
    // been called by main() before the splash screen so GP18 is high
    // and the upstream device has had time to boot.
    board_init();
    power_on_usb_a();

    // Wire m2host platform contract before tusb_init so the very first
    // mount callback already has a fully-configured Host.
    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setRngFn(platform_rng_fn);
    midi.begin();

    // Bring up TinyUSB host on rhport 1 (PIO-USB on GP16/GP17). The
    // Pico SDK + PIO-USB wiring is configured via tusb_config.h
    // (CFG_TUH_RPI_PIO_USB=1, BOARD_TUH_RHPORT=1).
    tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUH_RHPORT, &host_init);
}

// UMP word count per Message Type (top nibble of word 0). Indexed by MT.
static const uint8_t kMtWordCount[16] = {
    1, 1, 1, 2,   // 0,1,2,3
    2, 4, 1, 1,   // 4,5,6,7
    2, 2, 2, 3,   // 8,9,A,B
    3, 4, 4, 4    // C,D,E,F
};

void task(midi2::m2host& midi) {
    // Drain the USB host stack itself.
    tuh_task();

    // RX drain happens in tuh_midi2_rx_cb below.

    // Library housekeeping (CI Discovery timeout sweep).
    midi.task();

#if MIDI2CPP_HOST_WATCHDOG_MS > 0
    watchdog_tick(platform_now_fn());
#endif
}

}  // namespace feather_host

/*--------------------------------------------------------------------+
 * TinyUSB MIDI 2.0 Host callbacks (caller-supplied per midi2_host.h).
 *
 * mount/umount notify the m2host instance of lifecycle changes; the
 * m2host then auto-discovers (auto_discover=true by default) by
 * sending UMP Stream Endpoint Discovery + CI Discovery Inquiry. RX is
 * drained from task context (see feather_host::task above), so this
 * rx_cb is intentionally a no-op marker that keeps the linker happy
 * and reserves the hook for future ISR-aware deferral if needed.
 *--------------------------------------------------------------------*/
extern "C" {

void tuh_midi2_descriptor_cb(uint8_t idx,
                              const tuh_midi2_descriptor_cb_t* d) {
    // Capture bcdMSC ahead of mount_cb so notifyDeviceMounted has the
    // full picture. mount_cb does not carry bcdMSC.
    if (idx >= midi2::Host::MAX_DEVICES || !d) return;
    feather_host::g_bcdMSC[idx] =
        ((uint16_t)d->bcdMSC_hi << 8) | (uint16_t)d->bcdMSC_lo;
}

void tuh_midi2_mount_cb(uint8_t idx, const tuh_midi2_mount_cb_t* m) {
    if (!feather_host::g_midi) return;
    uint16_t bcd = (idx < midi2::Host::MAX_DEVICES)
                     ? feather_host::g_bcdMSC[idx] : 0x0200;
    feather_host::g_midi->notifyDeviceMounted(
        idx,
        /*protocolVersion*/   m->protocol_version,
        /*cableCount*/        m->rx_cable_count,
        /*altSettingActive*/  m->alt_setting_active,
        /*bcdMSC*/            bcd);
}

void tuh_midi2_rx_cb(uint8_t idx, uint32_t /*xferred_bytes*/) {
    if (!feather_host::g_midi) return;
    uint32_t buf[16];
    for (;;) {
        uint32_t n = tuh_midi2_ump_read(idx, buf, 16);
        if (n == 0) break;
        uint32_t i = 0;
        while (i < n) {
            uint8_t mt = (uint8_t)((buf[i] >> 28) & 0x0F);
            uint8_t wc = feather_host::kMtWordCount[mt];
            if (i + wc > n) break;
            feather_host::g_midi->feedRx(idx, &buf[i], wc);
            i += wc;
        }
    }
}

void tuh_midi2_tx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {
    // No-op for v0.1. App with its own pacing logic could hook here.
}

void tuh_midi2_umount_cb(uint8_t idx) {
    if (!feather_host::g_midi) return;
    feather_host::g_midi->notifyDeviceUnmounted(idx);
}

}  // extern "C"

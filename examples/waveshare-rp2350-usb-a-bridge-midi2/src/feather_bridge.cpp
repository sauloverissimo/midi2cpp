/*
 * feather_bridge.cpp: dual-stack TinyUSB platform glue for the bridge.
 *
 * Runs the host stack on rhport 1 (PIO-USB GP12/GP13) and the device
 * stack on rhport 0 (native USB-C) in the same firmware. Forwards UMP
 * between them through ump_router with single-message drain to avoid
 * saturating the destination TX FIFO.
 *
 * MIDI 1.0 fallback (alt=0 on the upstream device) is handled by
 * cable_event_to_ump: each USB-MIDI 1.0 cable event of CIN 0x8..0xE
 * becomes a UMP MT 0x2 carrying the same group/status/data.
 *
 * Hot-swap watchdog mirrors feather_host.cpp: tuh_deinit + tusb_init
 * after the upstream device has been gone for MIDI2CPP_BRIDGE_WATCHDOG_MS.
 */
#include "feather_bridge.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "class/midi/midi2_host.h"
#include "class/midi/midi2_device.h"

extern "C" {
#include "midi2.h"  /* midi2_msg_word_count */
}

#ifndef MIDI2CPP_BRIDGE_WATCHDOG_MS
#define MIDI2CPP_BRIDGE_WATCHDOG_MS 3000
#endif

namespace feather_bridge {

namespace {

// Per-idx state tracked from TinyUSB callbacks. Single-threaded loop,
// no synchronization needed.
uint8_t  g_alt_setting[MAX_HOST_DEVICES] = {0};
bool     g_idx_mounted[MAX_HOST_DEVICES] = {false};
uint8_t  g_active_idx                    = 0xFF;  // 0xFF = none
bool     g_device_mounted                = false;

HostMountFn       g_on_host_mount;
HostUnmountFn     g_on_host_unmount;
ForwardFn         g_on_fwd_upstream;
ForwardFn         g_on_fwd_downstream;
DeviceLifecycleFn g_on_device_mount;
DeviceLifecycleFn g_on_device_unmount;
DropFn            g_on_drop;

uint32_t g_last_drops_host   = 0;
uint32_t g_last_drops_device = 0;

#if MIDI2CPP_BRIDGE_WATCHDOG_MS > 0
bool     g_had_device      = false;
uint32_t g_devices_lost_ms = 0;
#endif

uint32_t now_ms() {
    return (uint32_t)(time_us_64() / 1000ULL);
}

// USB-MIDI 1.0 cable event word (LE: [CIN|Cable, status, data1, data2])
// to UMP MT 0x2 (1 word, packed: [0x2 | group | status | data1 | data2]).
// Returns false if the CIN is not a Channel Voice message we know how
// to lift; the bridge drops those in v0.1 (System Common, SysEx7, etc.).
bool cable_event_to_ump(uint32_t cable_event, uint32_t* ump_out) {
    uint8_t b0    = (uint8_t)(cable_event       & 0xFF);
    uint8_t b1    = (uint8_t)((cable_event >> 8)  & 0xFF);
    uint8_t b2    = (uint8_t)((cable_event >> 16) & 0xFF);
    uint8_t b3    = (uint8_t)((cable_event >> 24) & 0xFF);
    uint8_t cin   = b0 & 0x0F;
    uint8_t cable = (b0 >> 4) & 0x0F;

    if (cin < 0x8 || cin > 0xE) return false;

    *ump_out = ((uint32_t)0x2 << 28)
             | ((uint32_t)(cable & 0x0F) << 24)
             | ((uint32_t)b1 << 16)
             | ((uint32_t)b2 << 8)
             | (uint32_t)b3;
    return true;
}

// Drain RX from one mounted upstream idx and queue each UMP message
// on UMP_SOURCE_HOST for downstream forwarding. Respects word_count
// per MT so a multi-word read is split into individual messages.
void drain_upstream_rx(uint8_t idx) {
    uint32_t buf[16];
    while (true) {
        uint32_t n = tuh_midi2_ump_read(idx, buf, 16);
        if (n == 0) break;

        if (g_alt_setting[idx] == 0) {
            // MIDI 1.0 alt=0: each word is a USB cable event, lift to UMP.
            for (uint32_t i = 0; i < n; i++) {
                uint32_t ump;
                if (!cable_event_to_ump(buf[i], &ump)) continue;
                ump_router_push(UMP_SOURCE_HOST, &ump, 1);
            }
        } else {
            // MIDI 2.0 alt>=1: words are UMP. Walk by word_count.
            uint32_t i = 0;
            while (i < n) {
                uint8_t mt = (uint8_t)((buf[i] >> 28) & 0x0F);
                uint8_t wc = midi2_msg_word_count(mt);
                if (wc == 0 || i + wc > n) break;
                ump_router_push(UMP_SOURCE_HOST, &buf[i], wc);
                i += wc;
            }
        }
    }
}

// Drain RX from the device side (PC→bridge) and queue each UMP message
// on UMP_SOURCE_DEVICE for upstream forwarding. v0.1 only forwards
// when the device side is in alt=1 (UMP); alt=0 PC traffic is dropped.
void drain_downstream_rx() {
    if (tud_midi2_n_alt_setting(0) != 1) return;
    uint32_t buf[16];
    while (true) {
        uint32_t n = tud_midi2_n_ump_read(0, buf, 16);
        if (n == 0) break;
        uint32_t i = 0;
        while (i < n) {
            uint8_t mt = (uint8_t)((buf[i] >> 28) & 0x0F);
            uint8_t wc = midi2_msg_word_count(mt);
            if (wc == 0 || i + wc > n) break;
            ump_router_push(UMP_SOURCE_DEVICE, &buf[i], wc);
            i += wc;
        }
    }
}

// Pop one UMP from UMP_SOURCE_HOST and write it to the device side
// (PC). Drain a single message per call: in earlier production
// firmware on a similar dual-stack RP2040 setup, batch drain saturated
// the destination TX FIFO and the wire transmission stalled even
// though the write call returned success.
//
// We always pop, even when the destination is not ready (PC unmounted
// or in alt=0 USB-MIDI 1.0 mode), and silently drop the message in
// those cases. Buffering is the wrong contract here: a transparent
// bridge should not deliver UMP late once the link comes back up.
// Without this drain the ring buffer fills within seconds while the
// PC negotiates alt 1 and the on_drop callback fires.
void forward_upstream_to_device() {
    uint32_t words[4];
    uint8_t  count;
    if (!ump_router_pop(UMP_SOURCE_HOST, words, &count)) return;

    if (!g_device_mounted) return;
    if (tud_midi2_n_alt_setting(0) != 1) return;

    tud_midi2_n_ump_write(0, words, count);
    if (g_on_fwd_upstream) g_on_fwd_upstream(words, count);
}

// Pop one UMP from UMP_SOURCE_DEVICE and write it to the active
// upstream idx. v0.1 only forwards to MIDI 2.0 upstreams (alt>=1);
// MIDI 1.0 upstreams would need UMP→cable conversion, deferred.
//
// Same drain-on-not-ready rule as forward_upstream_to_device: we pop
// unconditionally and drop on the floor when the upstream is absent
// or in MIDI 1.0 mode, to keep the ring buffer healthy.
void forward_downstream_to_upstream() {
    uint32_t words[4];
    uint8_t  count;
    if (!ump_router_pop(UMP_SOURCE_DEVICE, words, &count)) return;

    if (g_active_idx == 0xFF) return;
    if (g_alt_setting[g_active_idx] == 0) return;  // v0.1: skip MIDI 1.0
    if (!tuh_midi2_mounted(g_active_idx)) return;

    tuh_midi2_ump_write(g_active_idx, words, count);
    tuh_midi2_write_flush(g_active_idx);
    if (g_on_fwd_downstream) g_on_fwd_downstream(words, count);
}

void surface_drops() {
    if (!g_on_drop) return;
    uint32_t h = ump_router_drop_count(UMP_SOURCE_HOST);
    uint32_t d = ump_router_drop_count(UMP_SOURCE_DEVICE);
    if (h != g_last_drops_host) {
        g_last_drops_host = h;
        g_on_drop(UMP_SOURCE_HOST, h);
    }
    if (d != g_last_drops_device) {
        g_last_drops_device = d;
        g_on_drop(UMP_SOURCE_DEVICE, d);
    }
}

#if MIDI2CPP_BRIDGE_WATCHDOG_MS > 0
void watchdog_tick(uint32_t t_ms) {
    bool any = false;
    for (uint8_t i = 0; i < MAX_HOST_DEVICES; ++i) {
        if (tuh_midi2_mounted(i)) { any = true; break; }
    }
    if (any) {
        g_had_device      = true;
        g_devices_lost_ms = 0;
        return;
    }
    if (!g_had_device) return;
    if (g_devices_lost_ms == 0) {
        g_devices_lost_ms = t_ms;
        return;
    }
    if (t_ms - g_devices_lost_ms < MIDI2CPP_BRIDGE_WATCHDOG_MS) return;

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

void init() {
    board_init();

    // The Waveshare RP2350-USB-A wires USB-A 5V directly from the
    // USB-C VBUS through a poly fuse; no software-controlled power
    // gate. Nothing to do here for this board, but keep the call
    // site so future boards with a power gate can drop their pin
    // here without touching the rest of init().

    ump_router_init();

    // Bring up device side first (rhport 0, native USB). The PC will
    // attempt to enumerate as soon as USB-C is plugged in.
    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    // Bring up host side (rhport 1, PIO-USB on GP12/GP13 for the
    // Waveshare RP2350-USB-A; the actual pin is set via
    // PICO_DEFAULT_PIO_USB_DP_PIN in CMakeLists.txt).
    tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUH_RHPORT, &host_init);
}

void task() {
    tuh_task();
    tud_task();

    // RX drain happens in tud_midi2_rx_cb / tuh_midi2_rx_cb below.

    // Forward 1 message in each direction per task() call. With a 1 ms
    // typical loop period this caps each direction at ~1 kmsg/s, well
    // above MIDI 2.0 bursts and well below USB FS bandwidth.
    forward_upstream_to_device();
    forward_downstream_to_upstream();

    surface_drops();

#if MIDI2CPP_BRIDGE_WATCHDOG_MS > 0
    watchdog_tick(now_ms());
#endif
}

bool upstream_present() {
    return g_active_idx != 0xFF && g_idx_mounted[g_active_idx];
}

bool downstream_present() {
    return g_device_mounted;
}

bool send_to_pc(const uint32_t* words, uint8_t count) {
    if (!g_device_mounted) return false;
    if (tud_midi2_n_alt_setting(0) != 1) return false;
    if (count == 0 || count > 4 || words == nullptr) return false;
    tud_midi2_n_ump_write(0, words, (uint32_t)count);
    return true;
}

void onHostMount(HostMountFn fn)             { g_on_host_mount     = std::move(fn); }
void onHostUnmount(HostUnmountFn fn)         { g_on_host_unmount   = std::move(fn); }
void onForwardUpstream(ForwardFn fn)         { g_on_fwd_upstream   = std::move(fn); }
void onForwardDownstream(ForwardFn fn)       { g_on_fwd_downstream = std::move(fn); }
void onDeviceMount(DeviceLifecycleFn fn)     { g_on_device_mount   = std::move(fn); }
void onDeviceUnmount(DeviceLifecycleFn fn)   { g_on_device_unmount = std::move(fn); }
void onDrop(DropFn fn)                       { g_on_drop           = std::move(fn); }

}  // namespace feather_bridge

/*--------------------------------------------------------------------+
 * TinyUSB callbacks, host side
 *--------------------------------------------------------------------*/
extern "C" {

void tuh_midi2_mount_cb(uint8_t idx, const tuh_midi2_mount_cb_t* m) {
    if (idx >= feather_bridge::MAX_HOST_DEVICES || !m) return;
    feather_bridge::g_alt_setting[idx] = m->alt_setting_active;
    feather_bridge::g_idx_mounted[idx] = true;
    if (feather_bridge::g_active_idx == 0xFF) {
        feather_bridge::g_active_idx = idx;
    }
    if (feather_bridge::g_on_host_mount) {
        feather_bridge::g_on_host_mount(idx, m->protocol_version);
    }
}

void tuh_midi2_umount_cb(uint8_t idx) {
    if (idx >= feather_bridge::MAX_HOST_DEVICES) return;
    feather_bridge::g_alt_setting[idx] = 0;
    feather_bridge::g_idx_mounted[idx] = false;
    if (feather_bridge::g_active_idx == idx) {
        feather_bridge::g_active_idx = 0xFF;
        for (uint8_t i = 0; i < feather_bridge::MAX_HOST_DEVICES; ++i) {
            if (feather_bridge::g_idx_mounted[i]) {
                feather_bridge::g_active_idx = i;
                break;
            }
        }
    }
    if (feather_bridge::g_on_host_unmount) {
        feather_bridge::g_on_host_unmount(idx);
    }
}

void tuh_midi2_descriptor_cb(uint8_t /*idx*/, const tuh_midi2_descriptor_cb_t* /*d*/) {
    // bcdMSC not needed by the bridge, the m2host class would consume
    // it but here we forward UMP raw, so no use.
}

void tuh_midi2_rx_cb(uint8_t idx, uint32_t /*xferred_bytes*/) {
    feather_bridge::drain_upstream_rx(idx);
}

void tuh_midi2_tx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {
    // No pacing logic on TX completion, drain is rate-limited by
    // task()'s "1 message per call" forwarding.
}

}  // extern "C"

/*--------------------------------------------------------------------+
 * TinyUSB callbacks, device side
 *--------------------------------------------------------------------*/
extern "C" {

void tud_mount_cb(void) {
    feather_bridge::g_device_mounted = true;
    if (feather_bridge::g_on_device_mount) feather_bridge::g_on_device_mount();
}

void tud_umount_cb(void) {
    feather_bridge::g_device_mounted = false;
    if (feather_bridge::g_on_device_unmount) feather_bridge::g_on_device_unmount();
}

void tud_suspend_cb(bool /*remote_wakeup_en*/) {
    feather_bridge::g_device_mounted = false;
}

void tud_resume_cb(void) {
    feather_bridge::g_device_mounted = true;
}

void tud_midi2_rx_cb(uint8_t /*itf*/) {
    feather_bridge::drain_downstream_rx();
}

void tud_midi2_set_itf_cb(uint8_t /*itf*/, uint8_t /*alt*/) {
    // Alt state polled via tud_midi2_n_alt_setting in drain/forward paths.
}

bool tud_midi2_get_req_itf_cb(uint8_t /*rhport*/,
                               const tusb_control_request_t* /*request*/) {
    return false;
}

// One bidirectional Function Block over Group 1. The #3738 built-in Stream
// responder derives the FB Info (direction + group span) from this GTB.
static const uint8_t k_gtb_desc[] = {
    TUD_MIDI2_GTB_HEADER(1),
    TUD_MIDI2_GTB_BLOCK(/*id*/ 1, MIDI2_GTB_BIDIRECTIONAL,
                        /*first_group*/ 0, /*num_groups*/ 1, /*stridx*/ 0),
};

const uint8_t* tud_midi2_gtb_desc_cb(uint8_t itf, uint16_t* len) {
    (void)itf;
    *len = (uint16_t)sizeof(k_gtb_desc);
    return k_gtb_desc;
}

const char* tud_midi2_fb_name_cb(uint8_t itf, uint8_t fb_idx) {
    (void)itf;
    return (fb_idx == 0) ? "Bridge" : "";
}

}  // extern "C"

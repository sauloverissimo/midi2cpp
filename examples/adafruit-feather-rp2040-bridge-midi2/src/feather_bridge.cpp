/*
 * feather_bridge.cpp: dual-stack TinyUSB platform glue for m2bridge.
 *
 * Runs the host stack on rhport 1 (PIO-USB GP16/GP17) and the device
 * stack on rhport 0 (native USB-C) in the same firmware. All bridge
 * behaviour lives in midi2::m2bridge; this file wires the TinyUSB
 * callbacks into it:
 *
 *   - device write fn: whole-message write with bounded tud_task pump
 *     (a batch drain saturates the destination TX FIFO on this board)
 *   - tud_midi2_rx_cb -> RX ring -> task() -> bridge.feedDeviceRx
 *   - tud_midi2_stream_msg_cb -> same ring (MT 0xF), returns HANDLED /
 *     NEGOTIATED_* so the driver's built-in responder stays silent and
 *     the bridge's multi-FB Stream responder owns the surface
 *   - tuh_midi2 mount/unmount -> host().notifyDeviceMounted/Unmounted
 *     (identity-bound slot placement inside the bridge)
 *   - MIDI 1.0 alt 0 upstream: slotSetActive + feedHostMidi1Bytes
 *     (the bridge's ByteStreamConverter does the uplift)
 *
 * Hot-swap watchdog: tuh_deinit + tusb_init after the upstream device
 * has been gone for MIDI2CPP_BRIDGE_WATCHDOG_MS.
 */
#include "feather_bridge.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/rand.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "class/midi/midi2_host.h"
#include "class/midi/midi2_device.h"

#include "midi2_rx_ring.h"

#ifndef MIDI2CPP_BRIDGE_WATCHDOG_MS
#define MIDI2CPP_BRIDGE_WATCHDOG_MS 3000
#endif

namespace feather_bridge {

namespace {

midi2::m2bridge* g_bridge = nullptr;

// Per-idx alt setting from the mount callback: 0 = MIDI 1.0 byte
// stream (uplift path), 1 = UMP.
uint8_t g_alt_setting[MIDI2CPP_HOST_MAX_DEVICES] = {0};

// SPSC ring: tud_midi2_rx_cb / tud_midi2_stream_msg_cb (producers, USB
// task context on this single-core loop) -> task() (consumer). Decode
// must not run inside the callback that drains the FIFO (the Stream/CI
// responders transmit from there). 64 slots fit a paginated PE GET.
midi2::RxRing<64> g_dev_rx_ring;

uint32_t now_ms() {
    return (uint32_t)(time_us_64() / 1000ULL);
}

size_t platform_dev_write_fn(const uint32_t* words, size_t count) {
    if (!tud_midi2_n_mounted(0)) return 0;
    if (tud_midi2_n_alt_setting(0) != 1) return 0;
    // Whole-message write: a full FIFO mid-burst must not truncate a
    // UMP. Pump tud_task() and retry, bounded (host gone or wedged).
    uint32_t off  = 0;
    uint32_t spin = 0;
    while (off < count) {
        off += tud_midi2_n_ump_write(0, words + off, (uint32_t)(count - off));
        if (off >= count) break;
        tud_task();
        if (++spin > 20000) break;
    }
    return (size_t)off;
}

size_t platform_host_write_fn(uint8_t idx, const uint32_t* words, size_t count) {
    if (!tuh_midi2_mounted(idx)) return 0;
    if (g_alt_setting[idx] == 0) return 0;  // no UMP->byte-stream path yet
    size_t n = tuh_midi2_ump_write(idx, words, (uint32_t)count);
    tuh_midi2_write_flush(idx);
    return n;
}

uint32_t platform_now_fn() { return now_ms(); }
uint32_t platform_rng_fn() { return get_rand_32(); }

#if MIDI2CPP_BRIDGE_WATCHDOG_MS > 0
bool     g_had_device      = false;
uint32_t g_devices_lost_ms = 0;

void watchdog_tick(uint32_t t_ms) {
    bool any = false;
    for (uint8_t i = 0; i < MIDI2CPP_HOST_MAX_DEVICES; ++i) {
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

void init(midi2::m2bridge& bridge) {
    g_bridge = &bridge;

    board_init();

    // Adafruit Feather RP2040 USB Host: drive USB-A 5V power gate.
    gpio_init(18);
    gpio_set_dir(18, GPIO_OUT);
    gpio_put(18, 1);

    bridge.setDownstreamWriteFn(platform_dev_write_fn);
    bridge.setUpstreamWriteFn(platform_host_write_fn);
    bridge.setNowFn(platform_now_fn);
    bridge.setRngFn(platform_rng_fn);
    bridge.begin();

    // Device side first (rhport 0, native USB): the PC enumerates as
    // soon as USB-C is plugged in.
    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    // Host side (rhport 1, PIO-USB on GP16/GP17).
    tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUH_RHPORT, &host_init);
}

void task(midi2::m2bridge& bridge) {
    tuh_task();
    tud_task();

    bool mounted = tud_midi2_n_mounted(0);
    bridge.setDeviceMounted(mounted);
    bridge.setDeviceAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    midi2::RxRing<64>::Slot slot;
    while (g_dev_rx_ring.pop(slot)) {
        bridge.feedDeviceRx(slot.ump, slot.words);
    }
    bridge.task();

#if MIDI2CPP_BRIDGE_WATCHDOG_MS > 0
    watchdog_tick(now_ms());
#endif
}

bool upstream_present() {
    for (uint8_t i = 0; i < MIDI2CPP_HOST_MAX_DEVICES; ++i) {
        if (tuh_midi2_mounted(i)) return true;
    }
    return false;
}

bool downstream_present() {
    return tud_midi2_n_mounted(0) && tud_midi2_n_alt_setting(0) == 1;
}

bool send_to_pc(const uint32_t* words, uint8_t count) {
    if (words == nullptr || count == 0 || count > 4) return false;
    return platform_dev_write_fn(words, count) == count;
}

}  // namespace feather_bridge

/*--------------------------------------------------------------------+
 * TinyUSB callbacks, host side
 *--------------------------------------------------------------------*/
extern "C" {

void tuh_midi2_mount_cb(uint8_t idx, const tuh_midi2_mount_cb_t* m) {
    if (idx >= MIDI2CPP_HOST_MAX_DEVICES || !m) return;
    if (!feather_bridge::g_bridge) return;
    feather_bridge::g_alt_setting[idx] = m->alt_setting_active;
    if (m->alt_setting_active == 0) {
        // MIDI 1.0 byte stream: platform-managed slot 0, bytes flow via
        // feedHostMidi1Bytes in tuh_midi2_rx_cb below.
        feather_bridge::g_bridge->slotSetActive(0, true, 0);
        return;
    }
    feather_bridge::g_bridge->host().notifyDeviceMounted(
        idx, m->protocol_version, m->rx_cable_count,
        m->alt_setting_active, 0x0200);
}

void tuh_midi2_umount_cb(uint8_t idx) {
    if (idx >= MIDI2CPP_HOST_MAX_DEVICES) return;
    if (!feather_bridge::g_bridge) return;
    if (feather_bridge::g_alt_setting[idx] == 0) {
        feather_bridge::g_bridge->slotSetActive(0, false, 0);
    } else {
        feather_bridge::g_bridge->host().notifyDeviceUnmounted(idx);
    }
    feather_bridge::g_alt_setting[idx] = 0;
}

void tuh_midi2_descriptor_cb(uint8_t /*idx*/, const tuh_midi2_descriptor_cb_t* /*d*/) {
}

void tuh_midi2_rx_cb(uint8_t idx, uint32_t /*xferred_bytes*/) {
    if (!feather_bridge::g_bridge) return;
    uint32_t buf[16];
    while (true) {
        uint32_t n = tuh_midi2_ump_read(idx, buf, 16);
        if (n == 0) break;
        if (feather_bridge::g_alt_setting[idx] == 0) {
            // Each word is a USB-MIDI 1.0 cable event; the bridge's
            // ByteStreamConverter uplifts it (RP2040 is little-endian,
            // the in-memory bytes ARE the packet bytes).
            feather_bridge::g_bridge->feedHostMidi1Bytes(
                0, reinterpret_cast<const uint8_t*>(buf), n * 4);
        } else {
            feather_bridge::g_bridge->feedHostRx(idx, buf, n);
        }
    }
}

void tuh_midi2_tx_cb(uint8_t /*idx*/, uint32_t /*xferred_bytes*/) {}

}  // extern "C"

/*--------------------------------------------------------------------+
 * TinyUSB callbacks, device side
 *--------------------------------------------------------------------*/
extern "C" {

void tud_midi2_rx_cb(uint8_t /*itf*/) {
    uint32_t buf[16];
    while (true) {
        uint32_t n = tud_midi2_n_ump_read(0, buf, 16);
        if (n == 0) break;
        uint32_t i = 0;
        while (i < n) {
            uint8_t wc = midi2_msg_word_count((uint8_t)((buf[i] >> 28) & 0x0F));
            if (wc == 0 || i + wc > n) break;
            feather_bridge::g_dev_rx_ring.push(0, &buf[i], wc);
            i += wc;
        }
    }
}

// Intercept every UMP Stream message: enqueue it for the bridge's
// multi-FB responder and keep the driver's built-in responder silent.
// Stream Config Requests return NEGOTIATED_* so the driver still
// records the protocol for tud_midi2_n_protocol.
tud_midi2_stream_result_t tud_midi2_stream_msg_cb(uint8_t /*itf*/,
                                                  const uint32_t* ump_words) {
    feather_bridge::g_dev_rx_ring.push(0, ump_words, 4);
    uint16_t status = (uint16_t)((ump_words[0] >> 16) & 0x3FF);
    if (status == 0x005) {   // Stream Configuration Request
        uint8_t protocol = (uint8_t)((ump_words[0] >> 8) & 0xFF);
        return (protocol == 0x01) ? MIDI2_STREAM_NEGOTIATED_MIDI1
                                  : MIDI2_STREAM_NEGOTIATED_MIDI2;
    }
    return MIDI2_STREAM_HANDLED;
}

void tud_midi2_set_itf_cb(uint8_t /*itf*/, uint8_t /*alt*/) {}

bool tud_midi2_get_req_itf_cb(uint8_t /*rhport*/,
                               const tusb_control_request_t* /*request*/) {
    return false;
}

// One bidirectional Group Terminal Block spanning all 16 groups
// (M2-104 Appendix I): Function Blocks live inside it, declared
// dynamically by the bridge's Stream responder.
static const uint8_t k_gtb_desc[] = {
    TUD_MIDI2_GTB_HEADER(1),
    TUD_MIDI2_GTB_BLOCK(/*id*/ 1, MIDI2_GTB_BIDIRECTIONAL,
                        /*first_group*/ 0, /*num_groups*/ 16, /*stridx*/ 0),
};

const uint8_t* tud_midi2_gtb_desc_cb(uint8_t itf, uint16_t* len) {
    (void)itf;
    *len = (uint16_t)sizeof(k_gtb_desc);
    return k_gtb_desc;
}

}  // extern "C"

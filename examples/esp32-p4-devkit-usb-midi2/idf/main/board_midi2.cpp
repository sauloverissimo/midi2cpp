/*
 * board_midi2.cpp: generic board core (TinyUSB <-> midi2cpp glue).
 *
 * Owns: ESP32-P4 USB-OTG PHY init (USB_PHY_TARGET_INT, device role),
 * TinyUSB device driver install (with MIDI 2.0 class driver from PR
 * #3571), and the wiring between TinyUSB and midi2cpp via the five
 * public hooks. The application layer only sees `midi2::m2device` and
 * `midi2::m2ci` objects that are already alive.
 *
 * The Waveshare ESP32-P4-WIFI6-DEV-KIT exposes two USB-C jacks: the
 * one labelled "USB-Device" routes to the P4 internal PHY (used here
 * for USB MIDI 2.0 device); the one labelled "ToUART" routes the CH343
 * USB-Serial-JTAG bridge for console + flashing. The P4 also has two
 * USB-A jacks routed through the UTMI host PHY (not used in this
 * device-only recipe; see the future `esp32-p4-devkit-host-midi2` for
 * that path).
 */
#include "board_midi2.h"

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_private/usb_phy.h"
#include "soc/lp_system_struct.h"
#include "tusb.h"

namespace midi2_board {

midi2::m2device* g_midi = nullptr;

namespace {

constexpr const char* TAG = "esp32-p4-midi2";

// Outbound UMP, invoked by the library for every sendXxx() and the JR
// heartbeat. Forwards to TinyUSB MIDI 2.0 stream write.
void platform_write_fn(const uint32_t* words, size_t count) {
    // UMP writes require mounted + Alt Setting 1; the driver returns 0 otherwise.
    if (!tud_midi2_n_mounted(0) || tud_midi2_n_alt_setting(0) != 1) return;
    // Full TX FIFO mid-burst is backpressure, not an error: the USB task
    // drains on its own thread; yield and retry (bounded).
    uint32_t off = 0;
    uint32_t spin = 0;
    while (off < count) {
        off += tud_midi2_n_ump_write(0, words + off, (uint32_t)(count - off));
        if (off >= count) break;
        vTaskDelay(1);
        if (++spin > 100) return;     // ~100 ms: host gone
    }
}

// Monotonic millisecond clock used by the JR heartbeat.
uint32_t platform_now_fn() {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

// Entropy source for MUID generation / regeneration.
uint32_t platform_rng_fn() {
    return esp_random();
}

void usb_phy_init() {
    // P4 PHY swap: by default the OTG_FS controller (USB_WRAP) is wired
    // to PHY1 (GPIO26/27, not connected on the Waveshare USB-C jacks)
    // while USB-Serial-JTAG sits on PHY0 (GPIO24/25, the "USB-Device"
    // USB-C jack). Without the swap below, usb_new_phy succeeds but the
    // OTG signals never reach a connector and the host sees nothing.
    // After the swap, OTG_FS lands on GPIO24/25 (USB-Device jack) and
    // USB-Serial-JTAG falls back to GPIO26/27 (no jack, the chip
    // remains debuggable via the CH343 ToUART path).
    // Refs: usb_wrap_ll.h sw_usb_phy_sel bits, P4 TRM USB_WRAP section.
    LP_SYS.usb_ctrl.sw_hw_usb_phy_sel = 1;  // enable software PHY select
    LP_SYS.usb_ctrl.sw_usb_phy_sel    = 1;  // OTG_FS to PHY0 (USB-C jack)

    usb_phy_handle_t phy = nullptr;
    usb_phy_config_t cfg = {};
    cfg.controller        = USB_PHY_CTRL_OTG;
    cfg.target            = USB_PHY_TARGET_INT;
    cfg.otg_mode          = USB_OTG_MODE_DEVICE;
    cfg.otg_speed         = USB_PHY_SPEED_FULL;
    ESP_ERROR_CHECK(usb_new_phy(&cfg, &phy));
    ESP_LOGI(TAG, "USB-OTG internal PHY ready (device, full speed, PHY0)");
}

}  // namespace

// Dedicated FreeRTOS task that runs the TinyUSB device stack.
// Equivalent to what the Espressif esp_tinyusb wrapper would do, kept
// inline so this recipe owns the full bring-up without an extra
// component dependency.
void tinyusb_task(void* arg) {
    (void)arg;
    while (true) {
        tud_task();
        vTaskDelay(1);
    }
}

void init(midi2::m2device& midi, midi2::m2ci& ci) {
    usb_phy_init();

    g_midi = &midi;

    // TinyUSB device init. The MIDI 2.0 class driver registers itself
    // when CFG_TUD_MIDI2 is enabled in tusb_config.h.
    tusb_rhport_init_t dev_init = {};
    dev_init.role  = TUSB_ROLE_DEVICE;
    dev_init.speed = TUSB_SPEED_AUTO;
    if (!tusb_init(BOARD_TUD_RHPORT, &dev_init)) {
        ESP_LOGE(TAG, "tusb_init failed");
        return;
    }
    xTaskCreatePinnedToCore(tinyusb_task, "tinyusb",
                            /*stack*/ 8192,
                            /*param*/ nullptr,
                            /*prio*/ 5,
                            /*handle*/ nullptr,
                            /*core*/ 0);
    ESP_LOGI(TAG, "TinyUSB device task started");

    // Wire the midi2cpp platform hooks; mounted/alt are bootstrap values,
    // kept in sync by the polling in task().
    midi.setWriteFn(platform_write_fn);
    midi.setNowFn(platform_now_fn);
    midi.setMounted(false);
    midi.setAltSetting(0);
    ci.setRngFn(platform_rng_fn);
}

void task(midi2::m2device& midi) {
    // Drain RX here, outside the USB service context: feedRx runs the
    // MIDI-CI responder synchronously, and its reply path needs the USB
    // machinery free to flush the TX FIFO (see platform_write_fn).
    {
        uint32_t rxbuf[16];
        for (;;) {
            uint32_t rxn = tud_midi2_n_ump_read(0, rxbuf, 16);
            if (rxn == 0) break;
            midi.feedRx(rxbuf, rxn);
        }
    }
    // Refresh USB lifecycle state (mount + alt setting) into the
    // library every iteration. tud_midi2_set_itf_cb fires on alt
    // changes; polling here handles the initial mount path uniformly.
    bool mounted = tud_midi2_n_mounted(0);
    midi.setMounted(mounted);
    midi.setAltSetting(mounted ? tud_midi2_n_alt_setting(0) : 0);

    // RX drain happens in tud_midi2_rx_cb below.

    // Library housekeeping (heartbeat, deferred sends).
    midi.task();
}

// The P4 dev-kit ships without a user-controllable RGB LED on a fixed
// pin; existing onboard LEDs (PWR, TX, RX) are tied to the regulator
// or the CH343 UART bridge and are not user-driveable. The mounted
// indicator is a no-op here; the UART log shows the same information.
// Boards that add an external WS2812 can patch this function or
// override LED_STRIP_GPIO at build time.
void led_show_mounted(bool mounted) {
    (void)mounted;
}

}  // namespace midi2_board

/* TinyUSB MIDI 2.0 callbacks (midi2_device.h API). Minimal stubs; state
 * propagation is handled by polling in task(). */
extern "C" {

void tud_midi2_rx_cb(uint8_t itf) {
    (void)itf;
    // Intentionally empty: this callback runs inside the USB service
    // context. RX is drained in task(), where the CI reply path can apply
    // TX backpressure safely.
}

void tud_midi2_set_itf_cb(uint8_t itf, uint8_t alt) {
    (void)itf;
    (void)alt;
}

bool tud_midi2_get_req_itf_cb(uint8_t rhport,
                              const tusb_control_request_t* request) {
    (void)rhport;
    (void)request;
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
    return (fb_idx == 0) ? CFG_TUD_MIDI2_EP_NAME : "";
}

}  // extern "C"

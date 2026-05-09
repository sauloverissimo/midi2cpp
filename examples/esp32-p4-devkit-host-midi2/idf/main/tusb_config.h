/*
 * tusb_config.h, TinyUSB configuration for esp32-p4-devkit-host-midi2.
 *
 * Host-only MIDI 2.0 class. High speed (ESP32-P4 USB-OTG UTMI PHY,
 * rhport 1, USB-A jacks on the Waveshare WIFI6 dev kit).
 */
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------+
 * MCU selection (ESP32-P4 USB-OTG, UTMI PHY for host on USB-A jacks)
 *--------------------------------------------------------------------*/
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU            OPT_MCU_ESP32P4
#endif

/* P4 host uses rhport 1 (UTMI). rhport 0 (INT PHY) stays free for the
 * future bridge variant which adds a device on the USB-Device USB-C jack. */
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT        1
#endif

#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED     OPT_MODE_HIGH_SPEED
#endif

/*--------------------------------------------------------------------+
 * Common
 *--------------------------------------------------------------------*/
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_FREERTOS
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

#define CFG_TUH_ENABLED         1
#define CFG_TUH_MAX_SPEED       BOARD_TUH_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__((aligned(4)))
#endif

/*--------------------------------------------------------------------+
 * Host
 *--------------------------------------------------------------------*/
#define CFG_TUH_HUB             1
#define CFG_TUH_DEVICE_MAX      (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_ENUMERATION_BUFSIZE  256

/* Enable MIDI 2.0 Host class driver (from PR #3571 fork). The fork
 * also auto-uplifts MIDI 1.0 byte-stream cable events into UMP MT 0x2
 * when the upstream device negotiates Alt 0 (legacy USB MIDI 1.0).
 *
 * Up to 4 upstream devices per driver (matches MIDI2CPP_HOST_MAX_DEVICES
 * in midi2cpp). With CFG_TUH_MIDI=4 + CFG_TUH_MIDI2=4 the host can
 * pick up 4 legacy MIDI 1.0 + 4 MIDI 2.0 devices simultaneously, each
 * driver claiming only its matching protocol via the alt-walk defer
 * (experiment/midi-coexistence branch). */
#define CFG_TUH_MIDI            4
#define CFG_TUH_MIDI2           4

#define CFG_TUH_MIDI2_NUM_GROUPS           1
#define CFG_TUH_MIDI2_NUM_FUNCTION_BLOCKS  1

#define CFG_TUH_MIDI2_RX_BUFSIZE    512
#define CFG_TUH_MIDI2_TX_BUFSIZE    256
#define CFG_TUH_MIDI2_RX_EPSIZE     64
#define CFG_TUH_MIDI2_TX_EPSIZE     64

#ifdef __cplusplus
}
#endif

#endif

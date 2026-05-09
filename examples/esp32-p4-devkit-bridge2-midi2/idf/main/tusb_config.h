/*
 * tusb_config.h, TinyUSB configuration for esp32-p4-devkit-bridge2-midi2.
 *
 * Dual-stack: USB MIDI 2.0 device on rhport 0 (INT PHY, "USB-Device"
 * USB-C jack) and USB MIDI 2.0 host on rhport 1 (UTMI PHY, USB-A
 * jacks). Same wire layout as the v1 sibling at
 * ../../esp32-p4-devkit-bridge-midi2; the difference is on the firmware
 * side: this recipe consumes the reusable midi2::m2bridge class instead
 * of carrying the slot table + Stream Discovery responder + UMP forward
 * path inline.
 */
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU            OPT_MCU_ESP32P4
#endif

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT        0   /* INT PHY, USB-C device jack */
#endif
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT        1   /* UTMI PHY, USB-A host jacks */
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED
#endif
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED     OPT_MODE_HIGH_SPEED
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_FREERTOS
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

#define CFG_TUD_ENABLED         1
#define CFG_TUH_ENABLED         1
#define CFG_TUD_MAX_SPEED       BOARD_TUD_MAX_SPEED
#define CFG_TUH_MAX_SPEED       BOARD_TUH_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__((aligned(4)))
#endif

/* Device side */
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  64
#endif
#define CFG_TUD_MIDI2           1
// Multi-slot bridge: 4 upstream devices x 4 groups = 16 groups, 4 FBs (one per slot).
#define CFG_TUD_MIDI2_NUM_GROUPS           16
#define CFG_TUD_MIDI2_NUM_FUNCTION_BLOCKS  4
#define CFG_TUD_MIDI2_TX_BUFSIZE   512
#define CFG_TUD_MIDI2_RX_BUFSIZE   256
#define CFG_TUD_MIDI2_TX_EPSIZE    64
#define CFG_TUD_MIDI2_RX_EPSIZE    64

// EP / product identity (used by both built-in and user responder paths;
// the built-in is bypassed below and the app sends Endpoint Name +
// Product Instance ID itself, but the macros must still be defined for
// the fork's GTB descriptor + a few static fallbacks).
#define CFG_TUD_MIDI2_EP_NAME              "ESP32P4Bridge2"
#define CFG_TUD_MIDI2_PRODUCT_ID           "ESP32P4Bridge2-0001"

// Bridge needs per-FB group windows + dynamic FB Names; the built-in
// responder hardcodes one window per FB and never sends FB Names.
// Local fork patch (see external/tinyusb/src/class/midi/midi2_device.c)
// lets MT 0xF Stream messages flow through to the app via
// tud_midi2_n_ump_read so midi2cpp's m2device dispatcher fires
// onEndpointDiscovery / onFbDiscovery callbacks.
#define CFG_TUD_MIDI2_USER_RESPONDER       1

/* Host side */
#define CFG_TUH_HUB             1
#define CFG_TUH_DEVICE_MAX      (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_ENUMERATION_BUFSIZE  256
// Both MIDI 1.0 and MIDI 2.0 host drivers enabled (scenario C). The
// alt-walk bcdMSC defer on the experiment/midi-coexistence fork branch
// (commit 91a54581) makes the two drivers disjoint: each one walks all
// alt settings of the MIDIStreaming interface and only claims when the
// device matches its own protocol version. Each driver fires its own
// callback set (tuh_midi_* for legacy, tuh_midi2_* for MIDI 2.0).
//
// See https://github.com/sauloverissimo/tinyusb/tree/experiment/midi-coexistence
// Up to 4 upstream devices per driver (matches MIDI2CPP_HOST_MAX_DEVICES
// in midi2cpp). With CFG_TUH_MIDI=4 + CFG_TUH_MIDI2=4 the bridge can
// host 4 legacy MIDI 1.0 + 4 MIDI 2.0 devices simultaneously, each
// driver claiming only its matching protocol via the alt-walk defer
// (experiment/midi-coexistence branch).
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

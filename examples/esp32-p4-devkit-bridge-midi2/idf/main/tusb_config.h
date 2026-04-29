/*
 * tusb_config.h, TinyUSB configuration for esp32-p4-devkit-bridge-midi2.
 *
 * Dual-stack: USB MIDI 2.0 device on rhport 0 (INT PHY, "USB-Device"
 * USB-C jack) and USB MIDI 2.0 host on rhport 1 (UTMI PHY, USB-A
 * jacks). The bridge forwards UMP between the two stacks; either side
 * can be a producer or a consumer.
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
#define CFG_TUD_MIDI2_NUM_GROUPS           1
#define CFG_TUD_MIDI2_NUM_FUNCTION_BLOCKS  1
#define CFG_TUD_MIDI2_TX_BUFSIZE   512
#define CFG_TUD_MIDI2_RX_BUFSIZE   256
#define CFG_TUD_MIDI2_TX_EPSIZE    64
#define CFG_TUD_MIDI2_RX_EPSIZE    64

/* Host side */
#define CFG_TUH_HUB             1
#define CFG_TUH_DEVICE_MAX      (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_ENUMERATION_BUFSIZE  256
#define CFG_TUH_MIDI            1
#define CFG_TUH_MIDI2           1
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

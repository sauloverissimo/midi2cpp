/*
 * tusb_config.h — TinyUSB configuration for rp2040-midi2 (RP2040)
 *
 * Device-only MIDI 2.0 class. Full speed (RP2040 native USB).
 */
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------+
 * Board-specific (Pico SDK handles MCU macros via PICO_BOARD)
 *--------------------------------------------------------------------*/
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

/*--------------------------------------------------------------------+
 * Common
 *--------------------------------------------------------------------*/
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

/*--------------------------------------------------------------------+
 * Device
 *--------------------------------------------------------------------*/
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  64
#endif

/* Enable MIDI 2.0 Device class driver. */
#define CFG_TUD_MIDI2         1

/* MIDI 2.0 config: 1 Group. The single Function Block covering all groups
 * is derived from the GTB descriptor upstream (TinyUSB #3738), so it needs
 * no config macro. */
#define CFG_TUD_MIDI2_NUM_GROUPS           1

/* UMP Endpoint Name shown by the host. */
#define CFG_TUD_MIDI2_EP_NAME              "RP2040 MIDI 2.0"

/* TX buffer sized for short bursts of UMP words during playback. */

#ifdef __cplusplus
}
#endif

#endif

/*
 * tusb_config.h, TinyUSB configuration for xiao-samd21-midi2.
 *
 * Device-only MIDI 2.0 class. Full speed (SAMD21 USB peripheral, FS only).
 * Tier C scope: minimal core suitable for the SAMD21's 32 KB SRAM.
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------+
 * MCU + speed (BOARD_TUD_RHPORT and BOARD_TUD_MAX_SPEED come from BSP
 * board.h via family_support.cmake)
 *--------------------------------------------------------------------*/
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU            OPT_MCU_SAMD21
#endif

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT        0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED
#endif

/*--------------------------------------------------------------------+
 * Common
 *--------------------------------------------------------------------*/
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

#define CFG_TUD_ENABLED         1
#define CFG_TUD_MAX_SPEED       BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__((aligned(4)))
#endif

/*--------------------------------------------------------------------+
 * Device
 *--------------------------------------------------------------------*/
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  64
#endif

/* Enable MIDI 2.0 Device class. All other classes off to keep the
 * SAMD21's flash + SRAM footprint minimal.
 */
#define CFG_TUD_MIDI2           1

/* MIDI 2.0 config: 1 Function Block covering 1 Group. */
#define CFG_TUD_MIDI2_NUM_GROUPS           1
#define CFG_TUD_MIDI2_NUM_FUNCTION_BLOCKS  1

/* TX/RX buffers tuned smaller for SAMD21 (32 KB SRAM). The library
 * still handles bursts via its own retry-on-backpressure pattern; the
 * smaller buffer just means more frequent USB transactions, which is
 * fine for the showcase emission rate.
 */
#define CFG_TUD_MIDI2_TX_BUFSIZE   128
#define CFG_TUD_MIDI2_RX_BUFSIZE   128
#define CFG_TUD_MIDI2_TX_EPSIZE    64
#define CFG_TUD_MIDI2_RX_EPSIZE    64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */

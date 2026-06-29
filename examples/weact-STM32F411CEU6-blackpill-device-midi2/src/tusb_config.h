/*
 * tusb_config.h, TinyUSB configuration for
 * weact-STM32F411CEU6-blackpill-device-midi2.
 *
 * Device-only MIDI 2.0 class. Full speed (STM32F411 OTG_FS peripheral,
 * FS only). Channel Voice + Stream Discovery scope, comfortably within
 * the STM32F411CEU6 (128 KB SRAM, 512 KB flash).
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
#define CFG_TUSB_MCU            OPT_MCU_STM32F4
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

/* Enable MIDI 2.0 Device class. All other classes off; the recipe is
 * a clean MIDI 2.0 device showcase, no CDC or HID.
 */
#define CFG_TUD_MIDI2           1

/* MIDI 2.0 config: 1 Group. The single Function Block is derived from the
 * GTB descriptor upstream (TinyUSB #3738), so it needs no config macro. */
#define CFG_TUD_MIDI2_NUM_GROUPS           1

/* Endpoint and Product Instance names reported by the upstream
 * auto-responder. PR #3738 derives the Function Block (direction, name)
 * from the GTB descriptor and answers CFG_TUD_MIDI2_EP_NAME, so the
 * fork-only CFG_TUD_MIDI2_USER_RESPONDER gate is no longer needed: the app
 * stream responder in main.cpp only adds Device Identity on top. */
#define CFG_TUD_MIDI2_EP_NAME              "STM32F411 MIDI 2.0"
#define CFG_TUD_MIDI2_PRODUCT_ID           "STM32F411-MIDI2-showcase-0001"

/* TX/RX buffers stay at the BSP defaults. The STM32F411 has 128 KB SRAM
 * headroom, and the library uses retry-on-backpressure, so the showcase
 * emission rate is comfortably absorbed.
 */

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */

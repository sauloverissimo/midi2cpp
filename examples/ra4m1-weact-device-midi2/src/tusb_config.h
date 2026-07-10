/*
 * tusb_config.h: TinyUSB configuration for ra4m1-weact-device-midi2 (RA4M1)
 *
 * Device-only MIDI 2.0 class. Full speed (RA4M1 USBFS peripheral, FS
 * only). Minimal-core scope suitable for the RA4M1's 32 KB SRAM.
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------+
 * MCU + speed (BOARD_TUD_RHPORT and BOARD_TUD_MAX_SPEED come from the
 * RA family BSP via family_support.cmake)
 *--------------------------------------------------------------------*/
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU            OPT_MCU_RAXXX
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
 * RA4M1's flash + SRAM footprint minimal.
 */
#define CFG_TUD_MIDI2           1

/* MIDI 2.0 config: 1 Group. The single Function Block is derived from the
 * GTB descriptor upstream (TinyUSB #3738), so it needs no config macro. */
#define CFG_TUD_MIDI2_NUM_GROUPS           1

/* UMP Endpoint Name shown by the host. */
#define CFG_TUD_MIDI2_EP_NAME              "WeAct RA4M1 MIDI 2.0"

/* Product Instance Id reported by the #3738 built-in Stream responder. */
#define CFG_TUD_MIDI2_PRODUCT_ID           "WeActRA4M1-showcase-0001"

/* TX/RX buffers stay at the library default, tuned for the RA4M1's
 * 32 KB SRAM. The library handles bursts via its own
 * retry-on-backpressure pattern; smaller buffers just mean more
 * frequent USB transactions, which is fine for the showcase rate.
 */

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */

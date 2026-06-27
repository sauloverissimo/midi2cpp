/*
 * tusb_config.h, TinyUSB configuration for nrf52840-promicro-midi2.
 *
 * Device-only MIDI 2.0 class. Full speed (nRF52840 USB peripheral, FS only).
 * Standard-subset scope suitable for the nRF52840 (256 KB SRAM,
 * 1 MB flash). Buffers larger than the SAMD21 sibling because the chip
 * has the headroom and the TinyUSB BSP for nRF52 places its DMA buffers
 * in dedicated RAM regions.
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
#define CFG_TUSB_MCU            OPT_MCU_NRF5X
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

/* MIDI 2.0 config: 1 Function Block covering 1 Group. */
#define CFG_TUD_MIDI2_NUM_GROUPS           1
#define CFG_TUD_MIDI2_NUM_FUNCTION_BLOCKS  1

/* UMP Endpoint Name shown by the host. */
#define CFG_TUD_MIDI2_EP_NAME              "nRF52840 Pro Micro MIDI 2.0"

/* TX/RX buffers tuned for the nRF52840 SRAM headroom. The library
 * still uses retry-on-backpressure, so the larger buffers just give
 * more burst tolerance for SysEx / Property Exchange flows that the
 * tighter XIAO sibling could not absorb.
 */

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */

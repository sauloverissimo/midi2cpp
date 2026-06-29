/*
 * tusb_config.h: TinyUSB dual stack (host + device) on RP2350.
 *
 * Waveshare RP2350-USB-A topology:
 *   rhport 0, native USB-C (USB device)         → DAW / PC
 *   rhport 1, PIO-USB host on GP12/GP13         → MIDI 2.0 instrument
 *
 * Forwards UMP between the two so the board acts as a transparent
 * MIDI 2.0 bridge: the upstream device shows up on the PC as a 16-group
 * MIDI 2.0 endpoint named "waveshare-RP2350-USB-A bridge".
 *
 * Hardware modification required: desolder R13 (1.5 kOhm pull-up on
 * the USB-A D+ line) before the host port enumerates correctly.
 */
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

/*--------------------------------------------------------------------+
 * Roothub assignment
 *--------------------------------------------------------------------*/
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      1
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED
#endif
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_FULL_SPEED
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
#define CFG_TUH_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED
#define CFG_TUH_MAX_SPEED     BOARD_TUH_MAX_SPEED

/* PIO-USB lives on the second roothub. The Pico SDK's TinyUSB
 * integration consumes Pico-PIO-USB via this flag. */
#define CFG_TUH_RPI_PIO_USB   1

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

/*--------------------------------------------------------------------+
 * Device side (USB-C → DAW)
 *
 * 16 Groups so the device side can mirror any group the upstream
 * instrument emits without remap. 1 Function Block covers all groups,
 * matching the most common simple bridge setup.
 *--------------------------------------------------------------------*/
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE              64
#endif

#define CFG_TUD_MIDI                        0
#define CFG_TUD_MIDI2                       1

/* Function Block topology is derived from the GTB descriptor upstream
 * (TinyUSB #3738); no NUM_FUNCTION_BLOCKS macro needed. */
#define CFG_TUD_MIDI2_NUM_GROUPS            16

#define CFG_TUD_MIDI2_EP_NAME               "waveshare-RP2350-USB-A bridge"
#define CFG_TUD_MIDI2_PRODUCT_ID            "feather-bridge"

/*--------------------------------------------------------------------+
 * Host side (USB-A ← upstream MIDI 2.0 device)
 *
 * Only 1 device idx is forwarded in v0.1 (idx 0). The driver still
 * allocates 4 slots so descriptor parsing for composite/multi-iface
 * upstream devices works; the bridge just forwards from idx 0 and
 * surfaces a warning on the OLED if a second device shows up.
 *--------------------------------------------------------------------*/
#define CFG_TUH_HUB                         1
#define CFG_TUH_DEVICE_MAX                  4
#define CFG_TUH_ENUMERATION_BUFSIZE         512

#define CFG_TUH_MIDI                        0
#define CFG_TUH_MIDI2                       4
#define CFG_TUH_MIDI2_RX_BUFSIZE            512
#define CFG_TUH_MIDI2_TX_BUFSIZE            512

#ifdef __cplusplus
}
#endif

#endif

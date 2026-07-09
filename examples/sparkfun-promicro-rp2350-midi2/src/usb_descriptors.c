/*
 * usb_descriptors.c: USB Device descriptors for sparkfun-promicro-rp2350-midi2.
 *
 * Identification:
 *   USB VID:PID         0xCAFE:0x4075   (TinyUSB educational VID + project PID)
 *   Manufacturer string midi2.diy
 *   Product string      RP2350ProMicro
 *
 * Configuration:
 *   1 MIDI 2.0 class interface (Audio Control + MIDI Streaming, Alt 0 + Alt 1)
 */
#include <string.h>
#include "bsp/board_api.h"
#include "tusb.h"
#include "class/audio/audio.h"
#include "class/midi/midi.h"

/*--------------------------------------------------------------------+
 * Device descriptor
 *--------------------------------------------------------------------*/
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCAFE,
    .idProduct          = 0x4075,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/*--------------------------------------------------------------------+
 * Configuration descriptor (MIDI 2.0)
 *--------------------------------------------------------------------*/
enum {
    ITF_NUM_MIDI2 = 0,
    ITF_NUM_MIDI2_STREAMING,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MIDI2_DESC_LEN)
#define EPNUM_MIDI2_OUT   0x01
#define EPNUM_MIDI2_IN    0x81

static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MIDI2_DESCRIPTOR(ITF_NUM_MIDI2, 0, EPNUM_MIDI2_OUT, EPNUM_MIDI2_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_fs_configuration;
}

/*--------------------------------------------------------------------+
 * String descriptors
 *--------------------------------------------------------------------*/
enum {
    STRID_LANGID       = 0,
    STRID_MANUFACTURER = 1,
    STRID_PRODUCT      = 2,
    STRID_SERIAL       = 3,
};

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},   /* 0: English (0x0409)               */
    "midi2.diy",  /* 1: Manufacturer                   */
    "RP2350ProMicro",             /* 2: Product                        */
    NULL,                         /* 3: Serial, computed at runtime   */
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    switch (index) {
        case STRID_LANGID:
            memcpy(&_desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;

        case STRID_SERIAL:
            chr_count = board_usb_get_serial(_desc_str + 1, 32);
            break;

        default:
            if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
                return NULL;
            }
            const char *str = string_desc_arr[index];
            chr_count = strlen(str);
            const size_t max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
            if (chr_count > max_count) chr_count = max_count;
            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

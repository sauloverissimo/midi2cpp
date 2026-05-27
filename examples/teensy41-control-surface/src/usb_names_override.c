// midi2cpp / teensy41-control-surface: USB string descriptor override
//
// The cores fork ships PaulStoffregen's official override hook:
// usb_desc.c declares usb_string_manufacturer_name and
// usb_string_product_name as weak aliases to defaults. Any sketch is
// free to provide stronger symbols. We override Manufacturer and
// Product so the device announces itself as a midi2cpp control surface
// while keeping VID/PID 0x16C0:0x0485 (the Teensyduino slot for
// USB_TYPE = MIDI2 in the V-USB shared VID list).
//
// iSerial stays at the default (per-board chip serial).

#include "usb_names.h"

#define MIDI2CPP_MANUFACTURER 'g','i','t','h','u','b','.','c','o','m','/','s','a','u','l','o','v','e','r','i','s','s','i','m','o'
#define MIDI2CPP_MANUFACTURER_LEN 25

#define MIDI2CPP_PRODUCT 'T','e','e','n','s','y','4','1',' ','C','S'
#define MIDI2CPP_PRODUCT_LEN 11

struct usb_string_descriptor_struct usb_string_manufacturer_name = {
	2 + MIDI2CPP_MANUFACTURER_LEN * 2,
	3,
	{ MIDI2CPP_MANUFACTURER }
};

struct usb_string_descriptor_struct usb_string_product_name = {
	2 + MIDI2CPP_PRODUCT_LEN * 2,
	3,
	{ MIDI2CPP_PRODUCT }
};

/*
 * esp32_p4_devkit_midi2.h, public API of the esp32-p4-devkit-usb-midi2
 * board core.
 *
 * The application layer (showcase) consumes this header and never
 * touches tud_*, esp_*, or any USB symbol directly. After init, the
 * m2device + m2ci instances are wired to the platform USB stack through
 * midi2_cpp's five public hooks (setWriteFn, feedRx, setNowFn,
 * setMounted, CI::setRngFn). The app then registers callbacks, sends
 * UMPs, and calls task() in the FreeRTOS loop.
 *
 * Replicating this pattern for another ESP32 board is a matter of
 * writing <board>_midi2.{h,cpp} that exposes the same two-function
 * surface plus an LED helper if the board has one.
 */
#pragma once

#include "midi2_cpp.h"

namespace esp32_p4_devkit_midi2 {

// Boots USB-OTG PHY, installs TinyUSB device task, sets up the MIDI 2.0
// device class, and wires the five midi2_cpp platform hooks into the
// supplied m2device / m2ci. After this returns, the app can register
// callbacks, send UMPs, and call task() in its main loop.
//
// Must be called once from app_main(), before any midi.send* / ci.*
// calls.
void init(midi2::m2device& midi, midi2::m2ci& ci);

// Drains the USB stack (tud_task is run on its own FreeRTOS task by
// TinyUSB; this helper drains UMP RX into midi.feedRx and refreshes
// mounted/alt state). Call every iteration of the main loop.
void task(midi2::m2device& midi);

// Optional mounted indicator. The Waveshare ESP32-P4-WIFI6-DEV-KIT does
// not expose a user-controllable RGB LED on a fixed pin, so this is a
// no-op on the stock kit. Callers may patch the implementation if they
// add an external WS2812 or use one of the GPIO header pins.
void led_show_mounted(bool mounted);

}  // namespace esp32_p4_devkit_midi2

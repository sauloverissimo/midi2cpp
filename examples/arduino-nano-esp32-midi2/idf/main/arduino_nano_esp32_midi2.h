/*
 * arduino_nano_esp32_midi2.h, public API of the
 * arduino-nano-esp32-midi2 board core.
 *
 * The application layer (showcase) consumes this header and never
 * touches tud_*, esp_*, or any USB symbol directly. After init, the
 * m2device + m2ci instances are wired to the platform USB stack through
 * midi2cpp's five public hooks (setWriteFn, feedRx, setNowFn,
 * setMounted, CI::setRngFn). The app then registers callbacks, sends
 * UMPs, and calls task() in the FreeRTOS loop.
 */
#pragma once

#include "midi2cpp.h"

namespace arduino_nano_esp32_midi2 {

// Boots USB-OTG PHY, installs TinyUSB device task, sets up the MIDI 2.0
// device class, and wires the five midi2cpp platform hooks into the
// supplied m2device / m2ci. After this returns, the app can register
// callbacks, send UMPs, and call task() in its main loop.
void init(midi2::m2device& midi, midi2::m2ci& ci);

// Drains the USB RX path and refreshes mounted/alt state. Call every
// iteration of the main loop.
void task(midi2::m2device& midi);

// On-board LED feedback. The Arduino Nano ESP32 ships with a single
// orange LED on LED_BUILTIN (GPIO48 on most ESP32-S3 Pro Micro / Nano
// form factors). Lit while USB is mounted.
void led_show_mounted(bool mounted);

}  // namespace arduino_nano_esp32_midi2

/*
 * esp32_p4_devkit_host.h, public API of the esp32-p4-devkit-host-midi2
 * board core.
 *
 * The application layer (host monitor) consumes this header and never
 * touches tuh_*, esp_*, or any USB symbol directly. After init, the
 * m2host instance is wired to the platform USB stack through
 * midi2cpp's host hooks (setWriteFn, feedRx, setNowFn, setRngFn,
 * setMounted). The app then registers callbacks and calls task() in
 * the FreeRTOS loop.
 *
 * Replicating this pattern for another ESP32 board with native UTMI
 * host is a matter of writing <board>_host.{h,cpp} that exposes the
 * same two-function surface.
 */
#pragma once

#include "midi2cpp.h"

namespace esp32_p4_devkit_host {

// Boots the UTMI USB-OTG PHY (host role, high speed), installs the
// TinyUSB host task on rhport 1, and wires the four midi2cpp host
// hooks into the supplied m2host instance. After this returns, the
// app can register callbacks and call task() in its main loop.
//
// Must be called once from app_main(), before any midi.* calls.
void init(midi2::m2host& host);

// Drains the USB host stack and any inbound UMP traffic into
// host.feedRx; refreshes mounted state from the TinyUSB MIDI 2.0 host
// driver. Call every iteration of the main loop.
void task(midi2::m2host& host);

}  // namespace esp32_p4_devkit_host

cmake_minimum_required(VERSION 3.16)

# esp32-p4-devkit-bridge-midi2, dual-stack USB MIDI 2.0 bridge on the
# Waveshare ESP32-P4-WIFI6-DEV-KIT. Acts as a USB MIDI 2.0 device on
# rhport 0 (INT PHY, USB-Device USB-C jack) AND a USB MIDI 2.0 host on
# rhport 1 (UTMI PHY, USB-A jacks). Forwards UMP between any device
# plugged into a USB-A jack and the host PC connected to the USB-Device
# USB-C jack, so the PC sees the upstream device(s) through this
# bridge. Lives at midi2cpp/examples/esp32-p4-devkit-bridge-midi2;
# consumes the parent library directly from ../../../src.
#
# Build:
#   . $IDF_PATH/export.sh
#   cd examples/esp32-p4-devkit-bridge-midi2/idf
#   ./scripts/fetch_tinyusb.sh        # one-off: clones TinyUSB
#                                       experiment/midi-coexistence
#                                       branch into external/tinyusb
#                                       at a pinned SHA, overrides
#                                       built-in.
#   idf.py set-target esp32p4
#   idf.py build
#   idf.py -p /dev/ttyACM0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_p4_devkit_bridge_midi2 C CXX ASM)

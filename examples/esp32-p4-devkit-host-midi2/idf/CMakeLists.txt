cmake_minimum_required(VERSION 3.16)

# esp32-p4-devkit-host-midi2, USB MIDI 2.0 host monitor on the
# Waveshare ESP32-P4-WIFI6-DEV-KIT. Receives UMP from any device
# plugged into the USB-A jacks (UTMI PHY, rhport 1) and decodes the
# stream live on the UART console. Pair with any midi2cpp device
# recipe to validate cross-platform interop. Lives at
# midi2cpp/examples/esp32-p4-devkit-host-midi2; consumes the parent
# library directly from ../../../src.
#
# Build:
#   . $IDF_PATH/export.sh
#   cd examples/esp32-p4-devkit-host-midi2/idf
#   ./scripts/fetch_tinyusb.sh        # one-off: clones TinyUSB
#                                       experiment/midi-coexistence
#                                       branch into external/tinyusb
#                                       at a pinned SHA, overrides
#                                       built-in.
#   idf.py set-target esp32p4
#   idf.py build
#   idf.py -p /dev/ttyACM0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_p4_devkit_host_midi2 C CXX ASM)

cmake_minimum_required(VERSION 3.16)

# esp32-s3-devkitc-usb-midi2, full-spec USB MIDI 2.0 device showcase on
# the ESP32-S3-DevKitC-1. Headless, identity-only USB MIDI 2.0 endpoint
# with the same 22 s scene cycle as the Pico SDK rp2040-midi2 example.
# Lives at midi2cpp/examples/esp32-s3-devkitc-usb-midi2; consumes the
# parent library directly from ../../../src.
#
# Build:
#   . $IDF_PATH/export.sh
#   cd examples/esp32-s3-devkitc-usb-midi2/idf
#   ./scripts/fetch_tinyusb.sh        # one-off: clones TinyUSB PR #3571
#                                       fork into components/tinyusb at
#                                       a pinned SHA, overrides built-in.
#   idf.py set-target esp32s3
#   idf.py build
#   idf.py -p /dev/ttyACM0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_s3_devkitc_usb_midi2 C CXX ASM)

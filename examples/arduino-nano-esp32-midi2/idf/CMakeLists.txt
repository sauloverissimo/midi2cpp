cmake_minimum_required(VERSION 3.16)

# arduino-nano-esp32-midi2, full-spec USB MIDI 2.0 device showcase on
# the Arduino Nano ESP32 (ESP32-S3, native USB-OTG). Mirrors the Pico
# SDK rp2040-midi2 22 s scene cycle. Lives at
# midi2cpp/examples/arduino-nano-esp32-midi2; consumes the parent
# library directly from ../../../src.
#
# Build:
#   . $IDF_PATH/export.sh
#   cd examples/arduino-nano-esp32-midi2/idf
#   ./scripts/fetch_tinyusb.sh        # one-off: clones the TinyUSB fork
#                                       (MIDI 2.0 branch) into
#                                       external/tinyusb, overrides built-in.
#   idf.py set-target esp32s3
#   idf.py build
#   idf.py -p /dev/ttyACM0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(arduino_nano_esp32_midi2 C CXX ASM)

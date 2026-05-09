cmake_minimum_required(VERSION 3.16)

# t-display-s3-midi2, USB MIDI 2.0 device receiver with on-board piano
# visualization on the LilyGo T-Display S3 (ESP32-S3, ST7789 1.9" IPS
# 320x170 parallel 8-bit). The host (DAW, OS) sends UMP, the device
# mirrors note activity on the on-board display. No emission, no audio,
# no synth. Lives at midi2cpp/examples/t-display-s3-midi2; consumes the
# parent library directly from ../../../src.
#
# Build:
#   . $IDF_PATH/export.sh
#   cd examples/t-display-s3-midi2/idf
#   ./scripts/fetch_tinyusb.sh        # one-off: clones TinyUSB PR #3571
#                                       fork into external/tinyusb at a
#                                       pinned SHA. Skip if you already
#                                       have a working copy symlinked.
#   idf.py set-target esp32s3
#   idf.py build
#   idf.py -p /dev/ttyACM0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(t_display_s3_midi2 C CXX ASM)

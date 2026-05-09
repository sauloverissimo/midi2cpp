cmake_minimum_required(VERSION 3.16)

# esp32-p4-devkit-bridge2-midi2, dual-stack USB MIDI 2.0 bridge on the
# Waveshare ESP32-P4-WIFI6-DEV-KIT, built on the reusable midi2::m2bridge
# class. Same wire role as the v1 sibling at
# ../esp32-p4-devkit-bridge-midi2/ (USB device on rhport 0 + USB host on
# rhport 1, multi-FB topology, per-slot group window, MIDI 1.0 alt 0
# uplift), but the slot table + Stream Discovery responder + UMP forward
# path live inside midi2cpp/src/midi2_bridge.cpp instead of being
# carried inline by the recipe.
#
# PC sees PID 0x4095 / "ESP32P4Bridge2", distinct from the v1 sibling's
# 0x4092 / "ESP32P4Bridge", so the two firmwares can coexist on the
# same host for A/B comparison.
#
# Build:
#   . $IDF_PATH/export.sh
#   cd examples/esp32-p4-devkit-bridge2-midi2/idf
#   ./scripts/fetch_tinyusb.sh        # one-off: clones TinyUSB PR #3571
#                                       fork into external/tinyusb at a
#                                       pinned SHA (symlinked from the
#                                       host sibling).
#   idf.py set-target esp32p4
#   idf.py build
#   idf.py -p /dev/ttyACM0 flash monitor

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_p4_devkit_bridge2_midi2 C CXX ASM)

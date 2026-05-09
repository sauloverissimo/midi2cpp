# main component for esp32-s3-devkitc-usb-midi2.
#
# Pulls midi2_cpp directly from ../../../../src (parent library, not
# vendored). The TinyUSB PR #3571 fork is expected to live at
# ../components/tinyusb after running ./scripts/fetch_tinyusb.sh; ESP-IDF
# resolves local components ahead of the built-in path, so the fork wins.

set(MIDI2_CPP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../src")

idf_component_register(
    SRCS
        "main.cpp"
        "esp32_s3_devkitc_midi2.cpp"
        "${MIDI2_CPP_ROOT}/midi2_device.cpp"
        "${MIDI2_CPP_ROOT}/midi2_ci.cpp"
    INCLUDE_DIRS
        "."
        "${MIDI2_CPP_ROOT}"
    REQUIRES
        midi2
        tinyusb
        led_strip
        driver
        esp_timer
    PRIV_REQUIRES
        usb
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

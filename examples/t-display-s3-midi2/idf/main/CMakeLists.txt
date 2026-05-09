# main component for t-display-s3-midi2.
#
# Pulls midi2cpp directly from ../../../../src (parent library, not
# vendored). The TinyUSB PR #3571 fork lives at ../external/tinyusb
# (cloned by scripts/fetch_tinyusb.sh, or symlinked to a local working
# copy) and is exposed as the ESP-IDF component "tinyusb" through the
# shim at ../components/tinyusb. The on-board ST7789 piano UI lives in
# ../components/piano_display and depends on LovyanGFX as a managed
# component (see idf_component.yml).

set(MIDI2CPP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../src")

idf_component_register(
    SRCS
        "main.cpp"
        "t_display_s3_midi2.cpp"
        "${MIDI2CPP_ROOT}/midi2_device.cpp"
        "${MIDI2CPP_ROOT}/midi2_ci.cpp"
    INCLUDE_DIRS
        "."
        "${MIDI2CPP_ROOT}"
    REQUIRES
        midi2
        tinyusb
        piano_display
        driver
        esp_timer
    PRIV_REQUIRES
        usb
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

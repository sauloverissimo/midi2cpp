# main component for esp32-p4-devkit-usb-midi2.
#
# Pulls midi2cpp directly from ../../../../src (parent library, not
# vendored). TinyUSB upstream is expected to live at
# ../components/tinyusb after running ./scripts/fetch_tinyusb.sh;
# ESP-IDF resolves local components ahead of the built-in path, so the
# local copy wins.

set(MIDI2CPP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../src")

idf_component_register(
    SRCS
        "main.cpp"
        "esp32_p4_devkit_midi2.cpp"
        "${MIDI2CPP_ROOT}/midi2_device.cpp"
        "${MIDI2CPP_ROOT}/midi2_ci.cpp"
    INCLUDE_DIRS
        "."
        "${MIDI2CPP_ROOT}"
    REQUIRES
        midi2
        tinyusb
        driver
        esp_timer
    PRIV_REQUIRES
        usb
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

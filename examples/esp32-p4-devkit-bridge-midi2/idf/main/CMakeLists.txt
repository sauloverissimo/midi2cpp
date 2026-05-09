# main component for esp32-p4-devkit-bridge-midi2.
#
# Dual-stack: USB MIDI 2.0 device on rhport 0 (INT PHY, USB-Device USB-C
# jack) AND USB MIDI 2.0 host on rhport 1 (UTMI PHY, USB-A jacks).
# Pulls midi2cpp directly from ../../../../src (parent library, not
# vendored). The TinyUSB PR #3571 fork is expected to live at
# ../external/tinyusb after running ./scripts/fetch_tinyusb.sh; the
# shim at ../components/tinyusb wires its device + host sources into
# the build.

set(MIDI2CPP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../src")

idf_component_register(
    SRCS
        "main.cpp"
        "esp32_p4_devkit_bridge.cpp"
        "${MIDI2CPP_ROOT}/midi2_device.cpp"
        "${MIDI2CPP_ROOT}/midi2_ci.cpp"
        "${MIDI2CPP_ROOT}/midi2_host.cpp"
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

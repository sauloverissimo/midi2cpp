# components/tinyusb, ESP-IDF shim component for the TinyUSB PR #3571 fork.
#
# The fork is cloned raw into ../../external/tinyusb by
# scripts/fetch_tinyusb.sh. This shim picks the source files needed for
# a USB MIDI 2.0 device on ESP32-S3 (Synopsys DWC2 dcd) and registers
# them as a single ESP-IDF component named "tinyusb". The recipe's
# main component lists this name in REQUIRES.

set(TUSB_FORK "${CMAKE_CURRENT_LIST_DIR}/../../external/tinyusb")

if(NOT EXISTS "${TUSB_FORK}/src/tusb.h")
    message(FATAL_ERROR
        "TinyUSB fork not found at ${TUSB_FORK}. "
        "Run idf/scripts/fetch_tinyusb.sh from the recipe root.")
endif()

set(TUSB_SRC "${TUSB_FORK}/src")

idf_component_register(
    SRCS
        "${TUSB_SRC}/tusb.c"
        "${TUSB_SRC}/common/tusb_fifo.c"
        # Device stack
        "${TUSB_SRC}/device/usbd.c"
        "${TUSB_SRC}/class/midi/midi_device.c"
        "${TUSB_SRC}/class/midi/midi2_device.c"
        # Synopsys DWC2 DCD (ESP32-S3 USB-OTG IP)
        "${TUSB_SRC}/portable/synopsys/dwc2/dcd_dwc2.c"
        "${TUSB_SRC}/portable/synopsys/dwc2/dwc2_common.c"
        # Device descriptors (must be in same link unit as usbd.c so the
        # tud_descriptor_*_cb symbols are visible to the static archive)
        "${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c"
    INCLUDE_DIRS
        "${TUSB_SRC}"
        "${CMAKE_CURRENT_LIST_DIR}/../../main"   # main provides tusb_config.h
    REQUIRES
        freertos
        hal
        esp_hw_support
        esp_rom
        esp_mm
        soc
)

# Application also defines tud_descriptor_*_cb in usb_descriptors.c.
# Make the include path available so tusb internals can find tusb_config.h.
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../../main"
)

# The fork carries some warnings under -Werror that upstream allows; keep
# downgrade local to this component, never propagate to the main app.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)

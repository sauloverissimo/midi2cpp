# components/tinyusb, ESP-IDF shim component for the TinyUSB
# experiment/midi-coexistence branch (upstream master + alt-walk
# bcdMSC defer for MIDI 1.0 + 2.0 host coexistence + opt-in user
# responder).
#
# TinyUSB is cloned raw into ../../external/tinyusb by
# scripts/fetch_tinyusb.sh. This shim picks the source files needed
# for a USB MIDI 2.0 host on ESP32-P4 (Synopsys DWC2 hcd, UTMI PHY)
# and registers them as a single ESP-IDF component named "tinyusb".
# The recipe's main component lists this name in REQUIRES.

set(TUSB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../external/tinyusb")

if(NOT EXISTS "${TUSB_DIR}/src/tusb.h")
    message(FATAL_ERROR
        "TinyUSB not found at ${TUSB_DIR}. "
        "Run idf/scripts/fetch_tinyusb.sh from the recipe root.")
endif()

set(TUSB_SRC "${TUSB_DIR}/src")

idf_component_register(
    SRCS
        "${TUSB_SRC}/tusb.c"
        "${TUSB_SRC}/common/tusb_fifo.c"
        # Host stack
        "${TUSB_SRC}/host/usbh.c"
        "${TUSB_SRC}/host/hub.c"
        "${TUSB_SRC}/class/midi/midi_host.c"
        "${TUSB_SRC}/class/midi/midi2_host.c"
        # Synopsys DWC2 HCD (ESP32-P4 USB-OTG UTMI PHY)
        "${TUSB_SRC}/portable/synopsys/dwc2/hcd_dwc2.c"
        "${TUSB_SRC}/portable/synopsys/dwc2/dwc2_common.c"
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

target_include_directories(${COMPONENT_LIB} PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../../main"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)

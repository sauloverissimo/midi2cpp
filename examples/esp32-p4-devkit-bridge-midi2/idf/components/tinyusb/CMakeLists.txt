# components/tinyusb, ESP-IDF shim component for the TinyUSB PR #3571 fork.
#
# Dual-stack: device on rhport 0 (INT PHY, USB-Device USB-C jack) plus
# host on rhport 1 (UTMI PHY, USB-A jacks). Both sides share the
# Synopsys DWC2 portable layer. usb_descriptors.c lives here so the
# tud_descriptor_*_cb symbols are in the same archive as usbd.c.

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
        "${TUSB_SRC}/device/usbd_control.c"
        "${TUSB_SRC}/class/midi/midi_device.c"
        "${TUSB_SRC}/class/midi/midi2_device.c"
        # Host stack
        "${TUSB_SRC}/host/usbh.c"
        "${TUSB_SRC}/host/hub.c"
        "${TUSB_SRC}/class/midi/midi_host.c"
        "${TUSB_SRC}/class/midi/midi2_host.c"
        # Synopsys DWC2 (both DCD + HCD)
        "${TUSB_SRC}/portable/synopsys/dwc2/dcd_dwc2.c"
        "${TUSB_SRC}/portable/synopsys/dwc2/hcd_dwc2.c"
        "${TUSB_SRC}/portable/synopsys/dwc2/dwc2_common.c"
        # Device descriptors (same link unit as usbd.c)
        "${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c"
    INCLUDE_DIRS
        "${TUSB_SRC}"
        "${CMAKE_CURRENT_LIST_DIR}/../../main"
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

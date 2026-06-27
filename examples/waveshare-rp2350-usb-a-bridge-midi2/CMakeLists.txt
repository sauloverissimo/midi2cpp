cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# waveshare-rp2350-usb-a-bridge-midi2, transparent USB MIDI 2.0 bridge
# on the Waveshare RP2350-USB-A. Runs TinyUSB host on USB-A (PIO-USB on
# GP12/GP13) and TinyUSB device on USB-C (native USB) in the same
# firmware, forwards UMP between them, and shows live traffic on an
# optional 128x64 SSD1306 OLED over I2C1 (GP2/GP3 by default). Lives at
# midi2cpp/examples/waveshare-rp2350-usb-a-bridge-midi2; consumes the
# parent library directly from ../../src.
#
# Hardware modification required: desolder R13 (1.5 kOhm pull-up on
# the USB-A D+ line) before using the host port. See README.

# The Waveshare RP2350-USB-A is not in the Pico SDK board headers in
# this version. We use pico2 as the generic RP2350 target and override
# PIO_USB_DP_PIN_DEFAULT to GP12 below.
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull TinyUSB upstream, pinned by SHA.
include(FetchContent)
if(NOT DEFINED PICO_TINYUSB_PATH AND NOT DEFINED ENV{PICO_TINYUSB_PATH})
    FetchContent_Declare(
        tinyusb
        GIT_REPOSITORY https://github.com/sauloverissimo/tinyusb.git
        GIT_TAG        0ed10cf6df04167b72cb0e1c7b41c8edcf5c6d9a
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(tinyusb)
    set(PICO_TINYUSB_PATH "${tinyusb_SOURCE_DIR}"
        CACHE PATH "TinyUSB upstream")
endif()

# Pico-PIO-USB drives the second USB port on GP12/GP13 via PIO state
# machines. SOURCE_SUBDIR pointing to a path with no CMakeLists.txt so
# FetchContent_MakeAvailable populates without add_subdirectory.
if(NOT DEFINED PICO_PIO_USB_PATH AND NOT DEFINED ENV{PICO_PIO_USB_PATH})
    # Pin to the SHA TinyUSB upstream ships in
    # hw/mcu/raspberry_pi/Pico-PIO-USB. Released tag 0.7.1 is too old:
    # it predates Pico-PIO-USB PR #186 ("reduce handshake delay") and
    # without that fix the SET_INTERFACE control transfer that switches
    # a MIDI 2.0 device into Alt 1 fails on PIO-USB host.
    FetchContent_Declare(
        pico_pio_usb
        GIT_REPOSITORY https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
        GIT_TAG        675543bcc9baa8170f868ab7ba316d418dbcf41f
        SOURCE_SUBDIR  src
    )
    FetchContent_MakeAvailable(pico_pio_usb)
    set(PICO_PIO_USB_PATH "${pico_pio_usb_SOURCE_DIR}"
        CACHE PATH "Pico-PIO-USB library")
endif()

include(pico_sdk_import.cmake)

project(waveshare-rp2350-usb-a-bridge-midi2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# midi2 (C99), only midi2_msg_word_count is used by the router.
# ---------------------------------------------------------------------------
set(MIDI2CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")


# ---------------------------------------------------------------------------
# Showcase executable.
# ---------------------------------------------------------------------------
add_executable(waveshare-rp2350-usb-a-bridge-midi2-showcase
    ${MIDI2CPP_ROOT}/src/midi2.c
    src/main.cpp
    src/feather_bridge.cpp
    src/ump_router.c
    src/usb_descriptors.c
    src/display.c
)
target_include_directories(waveshare-rp2350-usb-a-bridge-midi2-showcase PRIVATE src ${MIDI2CPP_ROOT}/src)

target_link_libraries(waveshare-rp2350-usb-a-bridge-midi2-showcase
    PRIVATE
        pico_stdlib
        hardware_i2c            # optional SSD1306 over I2C1
        tinyusb_device
        tinyusb_host
        tinyusb_pico_pio_usb    # PIO-USB host bit-banging
        tinyusb_board
)

# Waveshare RP2350-USB-A: PIO-USB on GP12 (D+) / GP13 (D-).
# PICO_DEFAULT_PIO_USB_DP_PIN is referenced by TinyUSB BSP's family.c
# but not defined by the generic pico2 board header. Set it here so
# both the TinyUSB BSP path and the PIO-USB library see the right pin.
target_compile_definitions(waveshare-rp2350-usb-a-bridge-midi2-showcase PRIVATE
    PIO_USB_DP_PIN_DEFAULT=12
    PICO_DEFAULT_PIO_USB_DP_PIN=12
)

# Hot-swap watchdog window (ms). 0 disables. Default 3000 matches the
# README; override with -DMIDI2CPP_BRIDGE_WATCHDOG_MS=N.
if(DEFINED MIDI2CPP_BRIDGE_WATCHDOG_MS)
    target_compile_definitions(waveshare-rp2350-usb-a-bridge-midi2-showcase
        PRIVATE MIDI2CPP_BRIDGE_WATCHDOG_MS=${MIDI2CPP_BRIDGE_WATCHDOG_MS})
endif()

# Disable USB CDC stdio (the USB-C interface is dedicated to MIDI 2.0).
# Use UART on default GP0/GP1 @ 115200 for debug print.
pico_enable_stdio_usb(waveshare-rp2350-usb-a-bridge-midi2-showcase 0)
pico_enable_stdio_uart(waveshare-rp2350-usb-a-bridge-midi2-showcase 1)

pico_add_extra_outputs(waveshare-rp2350-usb-a-bridge-midi2-showcase)

cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# waveshare-rp2350-usb-a-midi2, USB MIDI 2.0 device example for the
# Waveshare RP2350-USB-A (RP2350, USB-C native + USB-A host port). This
# recipe drives only the USB-C side as a MIDI 2.0 device; the USB-A
# host capability of the board is exercised by the sibling
# waveshare-rp2350-usb-a-bridge-midi2 recipe.
#
# Lives at midi2cpp/examples/waveshare-rp2350-usb-a-midi2; consumes
# the parent library directly from ../../src. Mirrors rp2040-midi2 with
# PICO_BOARD set to pico2 (the SDK's generic RP2350 target; the
# Waveshare board has the same USB IP) and identification strings
# rebranded to RP2350UsbA.

# The Waveshare RP2350-USB-A is not in the Pico SDK board headers in
# this version, so we use pico2 as the generic RP2350 target. Override
# with -DPICO_BOARD=<your-rp2350-board> if you build against an SDK
# that ships a dedicated header.
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull TinyUSB from upstream (PR #3738, merged).
include(FetchContent)
if(NOT DEFINED PICO_TINYUSB_PATH AND NOT DEFINED ENV{PICO_TINYUSB_PATH})
    FetchContent_Declare(
        tinyusb
        GIT_REPOSITORY https://github.com/hathach/tinyusb.git
        GIT_TAG        536157cb98fbc40329c9695281506fe7f04e526f
        GIT_SHALLOW    FALSE
    )
    FetchContent_MakeAvailable(tinyusb)
    set(PICO_TINYUSB_PATH "${tinyusb_SOURCE_DIR}"
        CACHE PATH "TinyUSB source")
endif()

include(pico_sdk_import.cmake)

project(waveshare-rp2350-usb-a-midi2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# midi2cpp, built from the parent library tree.
# ---------------------------------------------------------------------------
set(MIDI2CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")


add_library(midi2cpp STATIC
    ${MIDI2CPP_ROOT}/src/midi2.c
    ${MIDI2CPP_ROOT}/src/midi2_device.cpp
    ${MIDI2CPP_ROOT}/src/midi2_ci.cpp
)
target_include_directories(midi2cpp PUBLIC ${MIDI2CPP_ROOT}/src)

# ---------------------------------------------------------------------------
# waveshare-rp2350-usb-a-midi2, board core (Pico SDK + TinyUSB glue +
# 5 hooks wired). Same code as rp2040-midi2 core, board target swapped.
# ---------------------------------------------------------------------------
add_library(waveshare-rp2350-usb-a-midi2 STATIC
    src/rp2040_midi2.cpp
    src/usb_descriptors.c
)
target_include_directories(waveshare-rp2350-usb-a-midi2 PUBLIC src)
target_link_libraries(waveshare-rp2350-usb-a-midi2
    PUBLIC
        midi2cpp
        pico_stdlib
        pico_rand              # get_rand_32, used by the RNG hook for MUID
        tinyusb_device
        tinyusb_board
)

# ---------------------------------------------------------------------------
# Showcase executable, minimal main exercising the core's public API.
# ---------------------------------------------------------------------------
add_executable(waveshare-rp2350-usb-a-midi2-showcase
    src/main.cpp
)
target_link_libraries(waveshare-rp2350-usb-a-midi2-showcase PRIVATE waveshare-rp2350-usb-a-midi2)

# Disable USB CDC stdio (USB-C is dedicated to MIDI 2.0). UART on
# default pins @ 115200 8N1 for debug print. The Waveshare RP2350-USB-A
# breaks UART0 TX out on GP0 and UART0 RX on GP1, same as the Pico.
pico_enable_stdio_usb(waveshare-rp2350-usb-a-midi2-showcase 0)
pico_enable_stdio_uart(waveshare-rp2350-usb-a-midi2-showcase 1)

pico_add_extra_outputs(waveshare-rp2350-usb-a-midi2-showcase)

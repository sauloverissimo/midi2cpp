cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# sparkfun-promicro-rp2350-midi2, full-spec USB MIDI 2.0 device example
# for the SparkFun Pro Micro RP2350 (RP2350, USB-C, Pro Micro form
# factor). Mirrors rp2040-midi2 with the board target swapped to
# sparkfun_promicro_rp2350 and the identity strings rebranded to
# RP2350ProMicro. Lives at midi2cpp/examples/sparkfun-promicro-rp2350-midi2;
# consumes the parent library directly from ../../src. Builds standalone
# with the Pico SDK toolchain.
#
# Owns Pico SDK + TinyUSB + midi2cpp wiring; consumer applications
# (player, controller, etc.) sit on top via the small public API in
# src/rp2040_midi2.h and never touch tud_*, pico_*, or any USB symbol.
# The bundled sparkfun-promicro-rp2350-midi2-showcase executable is one
# such application: a headless full-spec MIDI 2.0 demo.

set(PICO_BOARD sparkfun_promicro_rp2350 CACHE STRING "Board type")

# Pull TinyUSB from upstream (PR #3738, merged). The fetched
# tree lives in build/_deps/tinyusb-src and is consumed by Pico SDK via
# PICO_TINYUSB_PATH.
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

project(sparkfun-promicro-rp2350-midi2 C CXX ASM)
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
# sparkfun-promicro-rp2350-midi2, board core (Pico SDK + TinyUSB glue +
# 5 hooks wired). Same code as rp2040-midi2 core, board target swapped.
# ---------------------------------------------------------------------------
add_library(sparkfun-promicro-rp2350-midi2 STATIC
    src/rp2040_midi2.cpp
    src/usb_descriptors.c
)
target_include_directories(sparkfun-promicro-rp2350-midi2 PUBLIC src)
target_link_libraries(sparkfun-promicro-rp2350-midi2
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
add_executable(sparkfun-promicro-rp2350-midi2-showcase
    src/main.cpp
)
target_link_libraries(sparkfun-promicro-rp2350-midi2-showcase PRIVATE sparkfun-promicro-rp2350-midi2)

# Disable USB CDC stdio (USB is dedicated to MIDI 2.0). UART on default
# pins @ 115200 8N1 for debug print. The Pro Micro RP2350 exposes UART0
# TX on the pad labelled TX (GPIO 0) and UART0 RX on RX (GPIO 1).
pico_enable_stdio_usb(sparkfun-promicro-rp2350-midi2-showcase 0)
pico_enable_stdio_uart(sparkfun-promicro-rp2350-midi2-showcase 1)

pico_add_extra_outputs(sparkfun-promicro-rp2350-midi2-showcase)

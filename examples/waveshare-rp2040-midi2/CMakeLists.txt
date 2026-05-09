cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# waveshare-rp2040-midi2, USB MIDI 2.0 device example for the Waveshare
# RP2040 Zero (RP2040, USB-C, no display). Mirrors rp2040-midi2 with the
# board target swapped to waveshare_rp2040_zero and identification
# strings adjusted. Lives at midi2_cpp/examples/waveshare-rp2040-midi2;
# consumes the parent library directly from ../../src.
#
# Owns Pico SDK + TinyUSB + midi2_cpp wiring; consumer applications sit
# on top via the small public API in src/rp2040_midi2.h and never touch
# tud_*, pico_*, or any USB symbol. The bundled
# waveshare-rp2040-midi2-showcase executable is one such application: a
# headless full-spec MIDI 2.0 demo.

set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")

# Pull TinyUSB PR #3571 fork at configure time (MIDI 2.0 device + host
# class drivers, not yet merged upstream). Pinned by SHA for
# reproducibility; GIT_SHALLOW keeps the fetch around 5 MB. The fetched
# tree lives in build/_deps/tinyusb_fork-src and is consumed by Pico
# SDK via PICO_TINYUSB_PATH.
#
# Override available: pass -DPICO_TINYUSB_PATH=/path/to/local/fork to
# cmake to point at a working copy on disk (skips the fetch entirely).
include(FetchContent)
if(NOT DEFINED PICO_TINYUSB_PATH AND NOT DEFINED ENV{PICO_TINYUSB_PATH})
    FetchContent_Declare(
        tinyusb_fork
        GIT_REPOSITORY https://github.com/sauloverissimo/tinyusb.git
        GIT_TAG        31d730d8bb0b5c0832c5490378a2a2dd60ab72aa
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(tinyusb_fork)
    set(PICO_TINYUSB_PATH "${tinyusb_fork_SOURCE_DIR}"
        CACHE PATH "TinyUSB fork (PR #3571)")
endif()

include(pico_sdk_import.cmake)

project(waveshare-rp2040-midi2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# midi2_cpp, built from the parent library tree.
# ---------------------------------------------------------------------------
set(MIDI2_CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# midi2 C99 core, pulled externally so the recipe shares one source
# of truth with the rest of the ecosystem. Override with
# -DMIDI2_LOCAL_PATH=/path/to/midi2 for offline builds.
include(FetchContent)
if(NOT TARGET midi2)
    if(DEFINED MIDI2_LOCAL_PATH)
        FetchContent_Declare(midi2 SOURCE_DIR ${MIDI2_LOCAL_PATH})
    else()
        FetchContent_Declare(midi2
            GIT_REPOSITORY https://github.com/sauloverissimo/midi2.git
            GIT_TAG        v0.3.2
            GIT_SHALLOW    TRUE
        )
    endif()
    FetchContent_MakeAvailable(midi2)
endif()

add_library(midi2_cpp STATIC
    ${MIDI2_CPP_ROOT}/src/midi2_device.cpp
    ${MIDI2_CPP_ROOT}/src/midi2_ci.cpp
)
target_include_directories(midi2_cpp PUBLIC ${MIDI2_CPP_ROOT}/src)
target_link_libraries(midi2_cpp PUBLIC midi2::midi2)

# ---------------------------------------------------------------------------
# waveshare-rp2040-midi2, board core (Pico SDK + TinyUSB glue +
# 5 hooks wired). Same code as rp2040-midi2 core, board target swapped.
# ---------------------------------------------------------------------------
add_library(waveshare-rp2040-midi2 STATIC
    src/rp2040_midi2.cpp
    src/usb_descriptors.c
)
target_include_directories(waveshare-rp2040-midi2 PUBLIC src)
target_link_libraries(waveshare-rp2040-midi2
    PUBLIC
        midi2_cpp
        pico_stdlib
        pico_rand              # get_rand_32, used by the RNG hook for MUID
        tinyusb_device
        tinyusb_board
)

# ---------------------------------------------------------------------------
# Showcase executable, minimal main exercising the core's public API.
# ---------------------------------------------------------------------------
add_executable(waveshare-rp2040-midi2-showcase
    src/main.cpp
)
target_link_libraries(waveshare-rp2040-midi2-showcase PRIVATE waveshare-rp2040-midi2)

# Disable USB CDC stdio (USB is dedicated to MIDI 2.0). UART on default
# pins @ 115200 8N1 for debug print. The Waveshare RP2040 Zero exposes
# UART0 TX on GP0 and UART0 RX on GP1, same as the Pico.
pico_enable_stdio_usb(waveshare-rp2040-midi2-showcase 0)
pico_enable_stdio_uart(waveshare-rp2040-midi2-showcase 1)

pico_add_extra_outputs(waveshare-rp2040-midi2-showcase)

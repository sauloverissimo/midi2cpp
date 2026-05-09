cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# rp2040-midi2 — full-spec USB MIDI 2.0 device example for Raspberry Pi
# Pico (RP2040). Lives at midi2cpp/examples/rp2040-midi2; consumes the
# parent library directly from ../../src. Builds standalone with the
# Pico SDK toolchain.
#
# Owns Pico SDK + TinyUSB + midi2cpp wiring; consumer applications
# (player, bridge, etc.) sit on top via the small public API in
# src/rp2040_midi2.h and never touch tud_*, pico_*, or any USB symbol.
# The bundled rp2040-midi2-showcase executable is one such application:
# a headless full-spec MIDI 2.0 demo.

set(PICO_BOARD pico CACHE STRING "Board type")

# Pull TinyUSB PR #3571 fork at configure time (MIDI 2.0 device + host
# class drivers, not yet merged upstream). Pinned by SHA for
# reproducibility; GIT_SHALLOW keeps the fetch around 5 MB. The fetched
# tree lives in build/_deps/tinyusb_fork-src and is consumed by Pico
# SDK via PICO_TINYUSB_PATH.
#
# The TinyUSB tree has no top-level CMakeLists.txt, so
# FetchContent_MakeAvailable populates without invoking add_subdirectory
# — exactly what we want, since Pico SDK builds TinyUSB its own way
# through family_support.cmake.
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

project(rp2040-midi2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# midi2cpp — built from the parent library tree (this example lives at
# midi2cpp/examples/rp2040-midi2, two directories above is the library
# root). No vendored copy needed.
# ---------------------------------------------------------------------------
set(MIDI2CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

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
            GIT_TAG        v0.3.3
            GIT_SHALLOW    TRUE
        )
    endif()
    FetchContent_MakeAvailable(midi2)
endif()

add_library(midi2cpp STATIC
    ${MIDI2CPP_ROOT}/src/midi2_device.cpp
    ${MIDI2CPP_ROOT}/src/midi2_ci.cpp
)
target_include_directories(midi2cpp PUBLIC ${MIDI2CPP_ROOT}/src)
target_link_libraries(midi2cpp PUBLIC midi2::midi2)

# ---------------------------------------------------------------------------
# rp2040-midi2 — board core (Pico SDK + TinyUSB glue + 5 hooks wired)
# ---------------------------------------------------------------------------
add_library(rp2040-midi2 STATIC
    src/rp2040_midi2.cpp
    src/usb_descriptors.c
)
target_include_directories(rp2040-midi2 PUBLIC src)
target_link_libraries(rp2040-midi2
    PUBLIC
        midi2cpp
        pico_stdlib
        pico_rand              # get_rand_32 — used by the RNG hook for MUID
        tinyusb_device
        tinyusb_board
)

# ---------------------------------------------------------------------------
# Showcase executable — minimal main exercising the core's public API.
# A real app (player, bridge, etc.) replaces this main with its own.
# ---------------------------------------------------------------------------
add_executable(rp2040-midi2-showcase
    src/main.cpp
)
target_link_libraries(rp2040-midi2-showcase PRIVATE rp2040-midi2)

# Disable USB CDC stdio (USB is dedicated to MIDI 2.0). Use UART on
# default GP0/GP1 @ 115200 for debug print.
pico_enable_stdio_usb(rp2040-midi2-showcase 0)
pico_enable_stdio_uart(rp2040-midi2-showcase 1)

pico_add_extra_outputs(rp2040-midi2-showcase)

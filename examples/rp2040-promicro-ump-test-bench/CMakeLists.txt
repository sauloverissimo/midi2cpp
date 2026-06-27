cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# rp2040-promicro-ump-test-bench, deterministic UMP test bench for the
# Windows MIDI Services consumer side. Targets the generic RP2040
# Pro Micro form factor (USB-C, 25 GPIOs exposed, BOOT + RESET only).
# Lives at midi2cpp/examples/rp2040-promicro-ump-test-bench; consumes the
# parent library directly from ../../src. Builds standalone with the
# Pico SDK toolchain.
#
# Owns Pico SDK + TinyUSB + midi2cpp wiring; the application layer
# (catalog + trigger logic) sits on top via the small public API in
# src/rp2040_midi2.h and never touches tud_*, pico_*, or any USB
# symbol. The bundled rp2040-promicro-ump-test-bench-showcase executable
# emits a 101-message UMP catalog on boot, accepts NoteOn group=15
# triggers for on-demand emission, and CC 120/121 group=15 to
# start/stop a single-message loop.

set(PICO_BOARD pico CACHE STRING "Board type")

# Pull TinyUSB upstream, pinned by SHA. The fetched tree lives in
# build/_deps/tinyusb-src and is consumed by Pico SDK via
# PICO_TINYUSB_PATH.
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

include(pico_sdk_import.cmake)

project(rp2040-promicro-ump-test-bench C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# midi2cpp, built from the parent library tree (this example lives at
# midi2cpp/examples/rp2040-promicro-ump-test-bench, two directories above is the
# library root). No vendored copy needed.
# ---------------------------------------------------------------------------
set(MIDI2CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")


add_library(midi2cpp STATIC
    ${MIDI2CPP_ROOT}/src/midi2.c
    ${MIDI2CPP_ROOT}/src/midi2_device.cpp
    ${MIDI2CPP_ROOT}/src/midi2_ci.cpp
)
target_include_directories(midi2cpp PUBLIC ${MIDI2CPP_ROOT}/src)

# ---------------------------------------------------------------------------
# rp2040-promicro-ump-test-bench, board core (Pico SDK + TinyUSB glue + 5 hooks
# wired). Same shape as rp2040-midi2; the recipe-specific application
# logic lives entirely in the showcase translation unit.
# ---------------------------------------------------------------------------
add_library(rp2040-promicro-ump-test-bench STATIC
    src/rp2040_midi2.cpp
    src/usb_descriptors.c
)
target_include_directories(rp2040-promicro-ump-test-bench PUBLIC src)
target_link_libraries(rp2040-promicro-ump-test-bench
    PUBLIC
        midi2cpp
        pico_stdlib
        pico_rand              # get_rand_32, used by the RNG hook for MUID
        tinyusb_device
        tinyusb_board
)

# ---------------------------------------------------------------------------
# Showcase executable, the catalog emitter + trigger handler.
# ---------------------------------------------------------------------------
add_executable(rp2040-promicro-ump-test-bench-showcase
    src/main.cpp
    src/catalog.cpp
)
target_link_libraries(rp2040-promicro-ump-test-bench-showcase PRIVATE rp2040-promicro-ump-test-bench)

# Stress flood build: emit a max-rate MIDI 2.0 sequence flood (seq in the
# velocity) instead of the catalog cycle, for the host-side zero-loss test.
# Configure with -DBENCH_AUTOFLOOD=ON; default OFF keeps the catalog emitter.
option(BENCH_AUTOFLOOD "Build the max-rate sequence flood emitter (stress) instead of the catalog cycle" OFF)
if(BENCH_AUTOFLOOD)
    target_compile_definitions(rp2040-promicro-ump-test-bench-showcase PRIVATE BENCH_AUTOFLOOD=1)
endif()

# Disable USB CDC stdio (USB is dedicated to MIDI 2.0). Use UART on
# default GP0/GP1 @ 115200 for the EMIT log lines.
pico_enable_stdio_usb(rp2040-promicro-ump-test-bench-showcase 0)
pico_enable_stdio_uart(rp2040-promicro-ump-test-bench-showcase 1)

pico_add_extra_outputs(rp2040-promicro-ump-test-bench-showcase)

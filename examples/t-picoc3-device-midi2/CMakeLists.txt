cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# t-picoc3-device-midi2 — full-spec USB MIDI 2.0 device example for
# the LilyGO T-PicoC3 (RP2040 + ESP32-C3 dual-chip, USB-C shared by
# orientation switch). The RP2040 owns the USB MIDI 2.0 device class;
# the ESP32-C3 stays parked (its own firmware path is out of scope here).
# Lives at midi2cpp/examples/t-picoc3-device-midi2; consumes the parent
# library directly from ../../src. Builds standalone with the Pico SDK.
#
# Owns Pico SDK + TinyUSB + midi2cpp + LovyanGFX wiring. Same shape as
# rp2040-midi2, plus a Scene Cinema visualisation on the on-board
# ST7789V 240x135 display (driven by the scene_display component).

set(PICO_BOARD pico CACHE STRING "Board type")

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

include(pico_sdk_import.cmake)

project(t-picoc3-device-midi2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# midi2cpp — built from the parent library tree.
# ---------------------------------------------------------------------------
set(MIDI2CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")


add_library(midi2cpp STATIC
    ${MIDI2CPP_ROOT}/src/midi2.c
    ${MIDI2CPP_ROOT}/src/midi2_device.cpp
    ${MIDI2CPP_ROOT}/src/midi2_ci.cpp
)
target_include_directories(midi2cpp PUBLIC ${MIDI2CPP_ROOT}/src)

# ---------------------------------------------------------------------------
# LovyanGFX — bare-metal-friendly TFT driver covering ST7789V on the
# T-PicoC3. Pico SDK supported via Bus_SPI + Panel_ST7789. Configured
# through lgfx_user.h in src/.
# ---------------------------------------------------------------------------
# Source-only clone. LovyanGFX's own CMakeLists.txt at the repo root
# re-imports pico_sdk_import.cmake and calls pico_sdk_init() in its own
# scope, which clashes with the top-level init above; CMake 4.x runs
# that script unconditionally from FetchContent_MakeAvailable/Populate.
# Cloning by hand keeps the sources around and bypasses the script.
set(lovyangfx_SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/lovyangfx-src)
if(NOT EXISTS ${lovyangfx_SOURCE_DIR}/CMakeLists.txt)
    find_package(Git REQUIRED)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} clone --depth 1 --branch 1.2.7
                https://github.com/lovyan03/LovyanGFX.git
                ${lovyangfx_SOURCE_DIR}
        RESULT_VARIABLE _lgfx_clone_rc
    )
    if(NOT _lgfx_clone_rc EQUAL 0)
        message(FATAL_ERROR "LovyanGFX clone failed (rc=${_lgfx_clone_rc})")
    endif()
endif()

file(GLOB LGFX_BASE_SOURCES   ${lovyangfx_SOURCE_DIR}/src/lgfx/v1/*.cpp)
file(GLOB LGFX_PANEL_SOURCES  ${lovyangfx_SOURCE_DIR}/src/lgfx/v1/panel/*.cpp)
file(GLOB LGFX_MISC_SOURCES   ${lovyangfx_SOURCE_DIR}/src/lgfx/v1/misc/*.cpp)
file(GLOB LGFX_RP2040_SOURCES ${lovyangfx_SOURCE_DIR}/src/lgfx/v1/platforms/rp2040/*.cpp)

add_library(lovyangfx STATIC
    ${LGFX_BASE_SOURCES}
    ${LGFX_PANEL_SOURCES}
    ${LGFX_MISC_SOURCES}
    ${LGFX_RP2040_SOURCES}
)
target_include_directories(lovyangfx PUBLIC ${lovyangfx_SOURCE_DIR}/src)
target_compile_definitions(lovyangfx PUBLIC
    LGFX_USE_V1=1
    USE_PICO_SDK=1
)
target_compile_options(lovyangfx PRIVATE
    -Wno-error=missing-field-initializers
    -Wno-missing-field-initializers
)
target_link_libraries(lovyangfx PUBLIC
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_pwm
    hardware_dma
)

# ---------------------------------------------------------------------------
# t-picoc3-device-midi2 — board core + scene_display visualiser.
# ---------------------------------------------------------------------------
add_library(t-picoc3-device-midi2 STATIC
    src/t_picoc3_midi2.cpp
    src/usb_descriptors.c
    src/scene_display.cpp
)
target_include_directories(t-picoc3-device-midi2 PUBLIC src)
target_link_libraries(t-picoc3-device-midi2
    PUBLIC
        midi2cpp
        lovyangfx
        pico_stdlib
        pico_multicore         # core1 runs the display render loop
        pico_rand              # get_rand_32 — used by the RNG hook for MUID
        tinyusb_device
        tinyusb_board
)

# ---------------------------------------------------------------------------
# Showcase executable — minimal main exercising the core's public API.
# ---------------------------------------------------------------------------
add_executable(t-picoc3-device-midi2-showcase
    src/main.cpp
)
target_link_libraries(t-picoc3-device-midi2-showcase PRIVATE t-picoc3-device-midi2)

# UART debug print is disabled: GP0/GP1 belong to the ST7789V SPI bus
# (TFT_RST=GP0, TFT_DC=GP1) on the T-PicoC3. All telemetry goes to the
# Scene Cinema display. The printf() calls compiled in become no-ops.
pico_enable_stdio_usb(t-picoc3-device-midi2-showcase 0)
pico_enable_stdio_uart(t-picoc3-device-midi2-showcase 0)

pico_add_extra_outputs(t-picoc3-device-midi2-showcase)

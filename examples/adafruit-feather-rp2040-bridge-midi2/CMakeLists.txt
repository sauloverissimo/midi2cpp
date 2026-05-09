cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# adafruit-feather-rp2040-bridge-midi2, transparent USB MIDI 2.0 bridge
# on the Adafruit Feather RP2040 USB Host. Runs TinyUSB host (PIO-USB on
# GP16/GP17) and TinyUSB device (native USB-C) in the same firmware,
# forwards UMP between them, and shows live traffic on a 128x64 SSD1306
# OLED over I2C1 (STEMMA QT). Lives at
# midi2cpp/examples/adafruit-feather-rp2040-bridge-midi2; consumes the
# parent library directly from ../../src.

set(PICO_BOARD adafruit_feather_rp2040_usb_host CACHE STRING "Board type")

# Pull TinyUSB PR #3571 fork (MIDI 2.0 host + device drivers, not yet
# merged upstream). Pinned by SHA for reproducibility; GIT_SHALLOW keeps
# the fetch around 5 MB. Exposed to Pico SDK via PICO_TINYUSB_PATH.
#
# Note on the pinned SHA: the fork branch may be rebased as the PR
# evolves, which can make this SHA disappear from the public branch
# tip. If the fetch fails with "object not found", bump GIT_TAG to a
# current commit on feat/midi2-device-host-driver, or use a local
# checkout via -DPICO_TINYUSB_PATH (see override below).
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

# Pico-PIO-USB drives the second USB port on GP16/GP17 via PIO state
# machines. SOURCE_SUBDIR pointing to a path with no CMakeLists.txt so
# FetchContent_MakeAvailable populates without add_subdirectory (the
# Pico SDK consumes the library by source path via PICO_PIO_USB_PATH,
# not as a CMake target).
if(NOT DEFINED PICO_PIO_USB_PATH AND NOT DEFINED ENV{PICO_PIO_USB_PATH})
    # Pin to the SHA the TinyUSB PR #3571 fork ships in
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

project(adafruit-feather-rp2040-bridge-midi2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# midi2 (C99), only midi2_msg_word_count is used by the router. Pull
# the single .c from the parent library tree to keep the binary lean.
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
            GIT_TAG        v0.3.4
            GIT_SHALLOW    TRUE
        )
    endif()
    FetchContent_MakeAvailable(midi2)
endif()

# ---------------------------------------------------------------------------
# Showcase executable.
# ---------------------------------------------------------------------------
add_executable(adafruit-feather-rp2040-bridge-midi2-showcase
    src/main.cpp
    src/feather_bridge.cpp
    src/ump_router.c
    src/usb_descriptors.c
    src/display.c
)
target_include_directories(adafruit-feather-rp2040-bridge-midi2-showcase PRIVATE src)

target_link_libraries(adafruit-feather-rp2040-bridge-midi2-showcase
    PRIVATE
        midi2::midi2
        pico_stdlib
        hardware_i2c            # SSD1306 over I2C1
        tinyusb_device
        tinyusb_host
        tinyusb_pico_pio_usb    # PIO-USB host bit-banging
        tinyusb_board
)

# Adafruit Feather RP2040 USB Host: PIO-USB on GP16/GP17.
# PICO_DEFAULT_PIO_USB_DP_PIN is referenced by TinyUSB BSP's family.c
# but not defined by the Pico SDK board header. Set it here so both
# the TinyUSB BSP path and the PIO-USB library see the right pin.
target_compile_definitions(adafruit-feather-rp2040-bridge-midi2-showcase PRIVATE
    PIO_USB_DP_PIN_DEFAULT=16
    PICO_DEFAULT_PIO_USB_DP_PIN=16
)

# Hot-swap watchdog window (ms). 0 disables. Default 3000 matches the
# README; override with -DMIDI2CPP_BRIDGE_WATCHDOG_MS=N.
if(DEFINED MIDI2CPP_BRIDGE_WATCHDOG_MS)
    target_compile_definitions(adafruit-feather-rp2040-bridge-midi2-showcase
        PRIVATE MIDI2CPP_BRIDGE_WATCHDOG_MS=${MIDI2CPP_BRIDGE_WATCHDOG_MS})
endif()

# Disable USB CDC stdio (the USB-C interface is dedicated to MIDI 2.0).
# Use UART on default GP0/GP1 @ 115200 for debug print.
pico_enable_stdio_usb(adafruit-feather-rp2040-bridge-midi2-showcase 0)
pico_enable_stdio_uart(adafruit-feather-rp2040-bridge-midi2-showcase 1)

pico_add_extra_outputs(adafruit-feather-rp2040-bridge-midi2-showcase)

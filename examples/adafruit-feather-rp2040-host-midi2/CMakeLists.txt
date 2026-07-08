cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# adafruit-feather-rp2040-host-midi2, full-spec USB MIDI 2.0 host
# example with SSD1306 display, on the Adafruit Feather RP2040 USB Host.
# Lives at midi2cpp/examples/adafruit-feather-rp2040-host-midi2;
# consumes the parent library directly from ../../src. Builds standalone
# with the Pico SDK toolchain + PIO-USB.

set(PICO_BOARD adafruit_feather_rp2040_usb_host CACHE STRING "Board type")

# Pull TinyUSB upstream, pinned by SHA.
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
        CACHE PATH "TinyUSB upstream")
endif()

# Pico-PIO-USB: bit-banged USB host on PIO state machines. Required
# because RP2040 only has one native USB IP block (used for USB-C
# device side, programming). PIO-USB drives the USB-A host port via
# GP16 (D+) / GP17 (D-) on the Adafruit Feather RP2040 USB Host.
#
# Pico-PIO-USB's root CMakeLists.txt uses pico_generate_pio_header()
# which is only defined after pico_sdk_init(). FetchContent_MakeAvailable
# would invoke add_subdirectory() too early. Use SOURCE_SUBDIR pointing
# to a path with no CMakeLists.txt so MakeAvailable populates without
# add_subdirectory. The Pico SDK's TinyUSB integration consumes the
# library via PICO_PIO_USB_PATH later.
if(NOT DEFINED PICO_PIO_USB_PATH AND NOT DEFINED ENV{PICO_PIO_USB_PATH})
    # Pin to the SHA TinyUSB upstream ships in
    # hw/mcu/raspberry_pi/Pico-PIO-USB. Released tag 0.7.1 is too old:
    # it predates Pico-PIO-USB PR #186 ("reduce handshake delay") and
    # without that fix the SET_INTERFACE control transfer that switches
    # a MIDI 2.0 device into Alt 1 fails on PIO-USB host, leaving the
    # link stuck on USB-MIDI 1.0 cable events.
    FetchContent_Declare(
        pico_pio_usb
        GIT_REPOSITORY https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
        GIT_TAG        675543bcc9baa8170f868ab7ba316d418dbcf41f
        SOURCE_SUBDIR  src   # contains no CMakeLists.txt → MakeAvailable
                              # populates but does not add_subdirectory
    )
    FetchContent_MakeAvailable(pico_pio_usb)
    set(PICO_PIO_USB_PATH "${pico_pio_usb_SOURCE_DIR}"
        CACHE PATH "Pico-PIO-USB library")
endif()

include(pico_sdk_import.cmake)

project(adafruit-feather-rp2040-host-midi2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# midi2cpp, built from the parent library tree (this example lives at
# midi2cpp/examples/adafruit-feather-rp2040-host-midi2). No vendored
# copy needed.
# ---------------------------------------------------------------------------
set(MIDI2CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")


add_library(midi2cpp STATIC
    ${MIDI2CPP_ROOT}/src/midi2.c
    ${MIDI2CPP_ROOT}/src/midi2_device.cpp
    ${MIDI2CPP_ROOT}/src/midi2_ci.cpp
    ${MIDI2CPP_ROOT}/src/midi2_host.cpp
)
target_include_directories(midi2cpp PUBLIC ${MIDI2CPP_ROOT}/src)

# ---------------------------------------------------------------------------
# Pico-PIO-USB sources compiled into our target. The library doesn't
# ship a CMakeLists.txt that fits the Pico SDK build flow; we compile
# the .c files directly and let the Pico SDK + TinyUSB pull them in
# via PICO_PIO_USB_PATH.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Showcase executable.
# ---------------------------------------------------------------------------
add_executable(adafruit-feather-rp2040-host-midi2-showcase
    src/main.cpp
    src/feather_host.cpp
    src/display.c
)
target_include_directories(adafruit-feather-rp2040-host-midi2-showcase PRIVATE src)

target_link_libraries(adafruit-feather-rp2040-host-midi2-showcase
    PRIVATE
        midi2cpp
        pico_stdlib
        pico_rand               # get_rand_32, host MUID entropy
        hardware_i2c            # SSD1306 over I2C1
        tinyusb_host
        tinyusb_pico_pio_usb    # PIO-USB host bit-banging
        tinyusb_board
)

# Stress build: pair with the flood emitter (rp2040-promicro-ump-test-bench
# built -DBENCH_AUTOFLOOD=ON). Skips the display and checks the inbound
# sequence for loss, printing a verdict on UART. Configure -DHOST_STRESS=ON.
option(HOST_STRESS "Build the zero-loss stress receiver (sequence verdict on UART) instead of the display monitor" OFF)
if(HOST_STRESS)
    target_compile_definitions(adafruit-feather-rp2040-host-midi2-showcase PRIVATE HOST_STRESS=1)
endif()

# Adafruit Feather RP2040 USB Host: PIO-USB on GP16/GP17.
# PICO_DEFAULT_PIO_USB_DP_PIN is referenced by TinyUSB BSP's family.c
# but not defined by the Pico SDK board header. Set it here so both
# the TinyUSB BSP path and the PIO-USB library see the right pin.
target_compile_definitions(adafruit-feather-rp2040-host-midi2-showcase PRIVATE
    PIO_USB_DP_PIN_DEFAULT=16
    PICO_DEFAULT_PIO_USB_DP_PIN=16
)

# Hot-swap watchdog window (ms). 0 disables the watchdog. Default of
# 3000 matches the README; override with -DMIDI2CPP_HOST_WATCHDOG_MS=N.
if(DEFINED MIDI2CPP_HOST_WATCHDOG_MS)
    target_compile_definitions(adafruit-feather-rp2040-host-midi2-showcase
        PRIVATE MIDI2CPP_HOST_WATCHDOG_MS=${MIDI2CPP_HOST_WATCHDOG_MS})
endif()

# Disable USB CDC stdio. Use UART on default GP0/GP1 @ 115200 for debug.
pico_enable_stdio_usb(adafruit-feather-rp2040-host-midi2-showcase 0)
pico_enable_stdio_uart(adafruit-feather-rp2040-host-midi2-showcase 1)

pico_add_extra_outputs(adafruit-feather-rp2040-host-midi2-showcase)

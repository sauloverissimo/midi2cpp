cmake_minimum_required(VERSION 3.20)

# ra4m1-weact-device-midi2, USB MIDI 2.0 device on the WeAct Studio
# RA4M1 64-Pin Core Board (Renesas R7FA4M1AB3CFM, Cortex-M4, 32 KB SRAM,
# 256 KB flash). Lives at midi2cpp/examples/ra4m1-weact-device-midi2;
# consumes the parent library directly from ../../src.
#
# Build system: TinyUSB native CMake (family_support.cmake) + ARM GNU
# toolchain (arm-none-eabi-gcc). TinyUSB is pulled via FetchContent
# from the fork's MIDI 2.0 branch (PR #3738).
#
# BSP choice: weact_ra4m1, a board derived from uno_r4 inside the
# TinyUSB fork (hw/bsp/ra/boards/weact_ra4m1). The Arduino UNO R4 uses
# the same R7FA4M1AB MCU and USBFS peripheral, but its linker reserves
# the first 0x4000 bytes for the UNO R4 DFU bootloader
# (FLASH_IMAGE_START = 0x4000 in memory_regions.ld), so a uno_r4 build
# links the image at 0x4000. The WeAct board has NO bootloader and boots
# from 0x0, so a uno_r4 image never runs (reset vector at 0x0 stays
# erased). The weact_ra4m1 board (in bsp/weact_ra4m1 of THIS recipe) is
# uno_r4 with that bootloader offset removed (links at 0x0) and LED1 on
# the on-board blue LED (P0.12). It is copied into the TinyUSB checkout
# at configure time below, so no upstream TinyUSB source file is
# modified. The internal HOCO 48 MHz clock (crystal independent) is kept.
# The on-chip USB LDO (VDCEN) is enabled in the board glue
# (src/weact_ra4m1_midi2.cpp).

# Pull TinyUSB BEFORE family_support.cmake processes BOARD/FAMILY, so
# the include path of family_support resolves to the fetched hw/bsp.
include(FetchContent)
if(NOT DEFINED TINYUSB_PATH AND NOT DEFINED ENV{TINYUSB_PATH})
    FetchContent_Declare(
        tinyusb
        GIT_REPOSITORY https://github.com/sauloverissimo/tinyusb.git
        GIT_TAG        fix/midi2-fb-direction
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(tinyusb)
    set(TINYUSB_PATH "${tinyusb_SOURCE_DIR}"
        CACHE PATH "TinyUSB source")
endif()

# TinyUSB RA BSPs depend on per-MCU SDK submodules: the Renesas FSP
# (Flexible Software Package, startup + clocks + IOPORT + USBFS HAL) and
# CMSIS_6 (Core headers for the RA Cortex-M4). The normal TinyUSB
# workflow runs `python tools/get_deps.py ra` to clone these into
# hw/mcu/renesas/fsp and lib/CMSIS_6. We trigger the same flow at
# configure time so a fresh clone produces a working build without
# manual steps.
if(NOT EXISTS "${TINYUSB_PATH}/hw/mcu/renesas/fsp/ra/fsp/src/bsp/mcu/all/bsp_clocks.c"
   OR NOT EXISTS "${TINYUSB_PATH}/lib/CMSIS_6/CMSIS/Core/Include/cmsis_compiler.h")
    message(STATUS "Fetching TinyUSB RA deps via tools/get_deps.py...")
    execute_process(
        COMMAND python3 "${TINYUSB_PATH}/tools/get_deps.py" ra
        WORKING_DIRECTORY "${TINYUSB_PATH}"
        RESULT_VARIABLE   GET_DEPS_RC
        OUTPUT_VARIABLE   GET_DEPS_OUT
        ERROR_VARIABLE    GET_DEPS_ERR
    )
    if(NOT GET_DEPS_RC EQUAL 0)
        message(FATAL_ERROR
            "TinyUSB get_deps.py ra failed (rc=${GET_DEPS_RC}):\n"
            "${GET_DEPS_OUT}\n${GET_DEPS_ERR}")
    endif()
endif()

# The weact_ra4m1 board lives in this recipe (bsp/weact_ra4m1). TinyUSB's
# family_support resolves BOARD only from hw/bsp/*/boards/, so install
# the board into the TinyUSB checkout at configure time. This adds a
# board directory without modifying any upstream TinyUSB source file
# (idempotent across re-configures).
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/bsp/weact_ra4m1"
     DESTINATION "${TINYUSB_PATH}/hw/bsp/ra/boards/")

# BOARD must be set BEFORE family_support.cmake; it dispatches to
# hw/bsp/<family>/family.cmake based on the BOARD name.
set(BOARD weact_ra4m1 CACHE STRING "TinyUSB BSP")

# Include family_support BEFORE project(); it sets CMAKE_TOOLCHAIN_FILE
# (arm-none-eabi-gcc) which has to be in place when project() runs.
include(${TINYUSB_PATH}/hw/bsp/family_support.cmake)

project(ra4m1-weact-device-midi2 C CXX ASM)

family_initialize_project(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR})

# ----------------------------------------------------------------------
# midi2cpp parent library (consumed via ../../src, not vendored)
# ----------------------------------------------------------------------
set(MIDI2CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")


add_library(midi2cpp STATIC
    ${MIDI2CPP_ROOT}/src/midi2.c
    ${MIDI2CPP_ROOT}/src/midi2_device.cpp
    ${MIDI2CPP_ROOT}/src/midi2_ci.cpp
)
target_include_directories(midi2cpp PUBLIC ${MIDI2CPP_ROOT}/src)
target_compile_features(midi2cpp PUBLIC cxx_std_17)
# midi2cpp uses std::function / std::array; exceptions stay off (default
# in arm-none-eabi-gcc with -fno-exceptions which TinyUSB build sets).
target_compile_options(midi2cpp PRIVATE -fno-rtti -fno-exceptions -fno-use-cxa-atexit)

# ----------------------------------------------------------------------
# Showcase executable
# ----------------------------------------------------------------------
add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
    src/main.cpp
    src/weact_ra4m1_midi2.cpp
    src/usb_descriptors.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE midi2cpp)

# C++17 + no exceptions/RTTI for the executable too (matches midi2cpp
# constraint and saves flash on the 256 KB RA4M1).
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions -fno-use-cxa-atexit>
)

# The RA family BSP (hw/bsp/ra/family.cmake) adds -ffreestanding PUBLIC,
# a Renesas FSP convention that propagates to this executable's own C++
# sources. -ffreestanding sets __STDC_HOSTED__=0, and the arm-none-eabi
# libstdc++ then hides std::function from <functional>, which midi2cpp's
# public API (m2device / m2ci callbacks) depends on. Re-assert the
# hosted macro for the C++ app sources so std::function resolves. The
# midi2cpp library target does not link the board BSP, so it already
# compiles hosted and needs no change. SAMD21 / nRF52 BSPs do not set
# -ffreestanding, so this workaround is RA-family specific.
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-D__STDC_HOSTED__=1>
)

# Hook into family_support: pulls in TinyUSB sources, the RA family.c
# board layer, the Renesas FSP (startup, clocks, IOPORT, USBFS HAL),
# the BSP linker script, and CMSIS_6 headers. Must come AFTER all
# target_sources / target_link_libraries calls.
family_configure_device_example(${PROJECT_NAME} noos)

# family_support sets WARN_FLAGS_GNU including C-only flags
# (-Wstrict-prototypes, -Wmissing-prototypes,
# -Werror-implicit-function-declaration) plus -Werror, which gcc
# rejects on C++ files. Disable -Werror on C++ so the
# "flag not applicable" warnings stay non-fatal. Library applies
# the same workaround.
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error>
)
target_compile_options(midi2cpp PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error>
)

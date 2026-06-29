cmake_minimum_required(VERSION 3.20)

# nrf52840-promicro-midi2, USB MIDI 2.0 device on Pro Micro nRF52840
# class boards (Nice!Nano, BlueMicro840, FYSETC nRF52840 Pro Micro,
# generic Pro Micro nRF52840 clones). Lives at
# midi2cpp/examples/nrf52840-promicro-midi2; consumes the parent
# library directly from ../../src.
#
# Build system: TinyUSB native CMake (family_support.cmake) + ARM GNU
# toolchain (arm-none-eabi-gcc). TinyUSB is pulled via FetchContent
# from the fork's MIDI 2.0 branch (PR #3738).
#
# BSP choice: feather_nrf52840_express. The TinyUSB upstream does not
# carry a Pro Micro nRF52840 / Nice!Nano BSP, but Feather Express uses
# the same MCU, the same Adafruit nRF52 UF2 bootloader region layout
# (FLASH ORIGIN 0x26000 in nrf52840_s140_v6.ld), and the same SoftDevice
# S140 v6 RAM reservation. The only divergence is the on-board LED pin
# (Feather: P1.15, Pro Micro genericos: P1.06/P1.07). Activity LED may
# light a non-visible pin; USB MIDI itself works identically.

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

# TinyUSB BSPs depend on per-MCU SDK submodules (CMSIS_5 for ARM core
# headers, nrfx for Nordic SDK startup + system files). The normal
# TinyUSB workflow runs `python tools/get_deps.py <family>` to clone
# these into hw/mcu/<vendor> and lib/CMSIS_5. We trigger the same flow
# at configure time so a fresh clone produces a working build without
# manual steps.
if(NOT EXISTS "${TINYUSB_PATH}/hw/mcu/nordic/nrfx/mdk/system_nrf52840.c"
   OR NOT EXISTS "${TINYUSB_PATH}/lib/CMSIS_5/CMSIS/Core/Include/cmsis_compiler.h")
    message(STATUS "Fetching TinyUSB nRF deps via tools/get_deps.py...")
    execute_process(
        COMMAND python3 "${TINYUSB_PATH}/tools/get_deps.py" nrf
        WORKING_DIRECTORY "${TINYUSB_PATH}"
        RESULT_VARIABLE   GET_DEPS_RC
        OUTPUT_VARIABLE   GET_DEPS_OUT
        ERROR_VARIABLE    GET_DEPS_ERR
    )
    if(NOT GET_DEPS_RC EQUAL 0)
        message(FATAL_ERROR
            "TinyUSB get_deps.py nrf failed (rc=${GET_DEPS_RC}):\n"
            "${GET_DEPS_OUT}\n${GET_DEPS_ERR}")
    endif()
endif()

# BOARD must be set BEFORE family_support.cmake; it dispatches to
# hw/bsp/<family>/family.cmake based on the BOARD name.
set(BOARD feather_nrf52840_express CACHE STRING "TinyUSB BSP")

# Include family_support BEFORE project(); it sets CMAKE_TOOLCHAIN_FILE
# (arm-none-eabi-gcc) which has to be in place when project() runs.
include(${TINYUSB_PATH}/hw/bsp/family_support.cmake)

project(nrf52840-promicro-midi2 C CXX ASM)

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
target_compile_options(midi2cpp PRIVATE -fno-rtti -fno-exceptions -fno-use-cxa-atexit)

# ----------------------------------------------------------------------
# Showcase executable
# ----------------------------------------------------------------------
add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
    src/main.cpp
    src/nrf52840_promicro_midi2.cpp
    src/usb_descriptors.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE midi2cpp)

# C++17 + no exceptions/RTTI for the executable too. nRF52840 has plenty
# of flash (1 MB) and SRAM (256 KB), so the constraint is for parity
# with the rest of the midi2cpp portfolio rather than for size.
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions -fno-use-cxa-atexit>
)

# Hook into family_support: pulls in TinyUSB sources, board.c, BSP
# linker script (nrf52840_s140_v6.ld), startup file, and CMSIS_5
# headers. Must come AFTER all target_sources / target_link_libraries
# calls.
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

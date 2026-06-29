cmake_minimum_required(VERSION 3.20)

# weact-STM32F411CEU6-blackpill-device-midi2, USB MIDI 2.0 device on the
# WeAct Studio STM32F411CEU6 BlackPill (V2.0 / V3.0 core board).
# Lives at midi2cpp/examples/weact-STM32F411CEU6-blackpill-device-midi2;
# consumes the parent library directly from ../../src.
#
# Build system: TinyUSB native CMake (family_support.cmake) + ARM GNU
# toolchain (arm-none-eabi-gcc). TinyUSB is pulled via FetchContent,
# tracking the fork's fix/midi2-fb-direction branch (PR #3738).
#
# BSP choice: stm32f411blackpill (in TinyUSB upstream). The BSP targets
# the WeAct Black Pill V2.0 directly (LED PC13 active-low, KEY PA0,
# 25 MHz HSE, OTG_FS device on PA11/PA12). MCU variant stm32f411xe,
# 512 KB flash, 128 KB SRAM. No custom bsp/ directory is needed because
# the board ships in TinyUSB hw/bsp/stm32f4/boards/.

# Pull TinyUSB BEFORE family_support.cmake processes BOARD/FAMILY, so the
# include path of family_support resolves to the fetched hw/bsp.
#
# Tracks the fork's fix/midi2-fb-direction branch (PR #3738). That PR
# derives the Function Block (direction, name) from the GTB descriptor and
# reports CFG_TUD_MIDI2_EP_NAME from the built-in auto-responder, which
# fixes the cross-platform name race the old fork pin worked around: SAMD21
# and STM32 used to LOSE the race and report "TinyUSB MIDI 2.0", so the
# fork-only CFG_TUD_MIDI2_USER_RESPONDER gate turned the auto-responder OFF.
# With #3738 the gate is gone; the app responder in main.cpp only adds
# Device Identity on top. Re-point to the upstream ref once #3738 lands.
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
        CACHE PATH "TinyUSB fork (PR #3738, fix/midi2-fb-direction)")
endif()

# TinyUSB BSPs depend on per-MCU SDK submodules (CMSIS_5 for ARM core
# headers, ST cmsis_device_f4 + stm32f4xx_hal_driver for the STM32F4
# startup, system files, and HAL). The normal TinyUSB workflow runs
# `python tools/get_deps.py <family>` to clone these into hw/mcu/st and
# lib/CMSIS_5. We trigger the same flow at configure time so a fresh
# clone produces a working build without manual steps. The stm32f4 arg
# fetches cmsis_device_f4 + stm32f4xx_hal_driver together.
if(NOT EXISTS "${TINYUSB_PATH}/hw/mcu/st/cmsis_device_f4/Source/Templates/system_stm32f4xx.c"
   OR NOT EXISTS "${TINYUSB_PATH}/hw/mcu/st/stm32f4xx_hal_driver/Inc/stm32f4xx_hal.h"
   OR NOT EXISTS "${TINYUSB_PATH}/lib/CMSIS_5/CMSIS/Core/Include/cmsis_compiler.h")
    message(STATUS "Fetching TinyUSB stm32f4 deps via tools/get_deps.py...")
    execute_process(
        COMMAND python3 "${TINYUSB_PATH}/tools/get_deps.py" stm32f4
        WORKING_DIRECTORY "${TINYUSB_PATH}"
        RESULT_VARIABLE   GET_DEPS_RC
        OUTPUT_VARIABLE   GET_DEPS_OUT
        ERROR_VARIABLE    GET_DEPS_ERR
    )
    if(NOT GET_DEPS_RC EQUAL 0)
        message(FATAL_ERROR
            "TinyUSB get_deps.py stm32f4 failed (rc=${GET_DEPS_RC}):\n"
            "${GET_DEPS_OUT}\n${GET_DEPS_ERR}")
    endif()
endif()

# BOARD must be set BEFORE family_support.cmake; it dispatches to
# hw/bsp/<family>/family.cmake based on the BOARD name.
set(BOARD stm32f411blackpill CACHE STRING "TinyUSB BSP")

# Include family_support BEFORE project(); it sets CMAKE_TOOLCHAIN_FILE
# (arm-none-eabi-gcc) which has to be in place when project() runs.
include(${TINYUSB_PATH}/hw/bsp/family_support.cmake)

project(weact-STM32F411CEU6-blackpill-device-midi2 C CXX ASM)

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
    src/weact_blackpill_f411_midi2.cpp
    src/usb_descriptors.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE midi2cpp)

# C++17 + no exceptions/RTTI for the executable too. STM32F411CEU6 has
# 512 KB flash and 128 KB SRAM, so the constraint is for parity with the
# rest of the midi2cpp portfolio rather than for size.
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions -fno-use-cxa-atexit>
)

# Hook into family_support: pulls in TinyUSB sources, board.c, BSP
# linker script (STM32F411CEUx_FLASH.ld), startup file, STM32F4 HAL, and
# CMSIS_5 headers. Must come AFTER all target_sources /
# target_link_libraries calls.
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

cmake_minimum_required(VERSION 3.20)

# xiao-samd21-midi2, USB MIDI 2.0 device on the Seeed Studio XIAO SAMD21.
# Lives at midi2_cpp/examples/xiao-samd21-midi2; consumes the parent
# library directly from ../../src.
#
# Build system: TinyUSB native CMake (family_support.cmake) + ARM GNU
# toolchain (arm-none-eabi-gcc). The TinyUSB PR #3571 fork is pulled
# via FetchContent at a pinned SHA. Same SHA used by Pico SDK and
# ESP-IDF recipes.

# Pull TinyUSB fork BEFORE family_support.cmake processes BOARD/FAMILY,
# so the include path of family_support resolves to our fork's hw/bsp.
include(FetchContent)
if(NOT DEFINED TINYUSB_FORK_PATH AND NOT DEFINED ENV{TINYUSB_FORK_PATH})
    FetchContent_Declare(
        tinyusb_fork
        GIT_REPOSITORY https://github.com/sauloverissimo/tinyusb.git
        GIT_TAG        31d730d8bb0b5c0832c5490378a2a2dd60ab72aa
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(tinyusb_fork)
    set(TINYUSB_FORK_PATH "${tinyusb_fork_SOURCE_DIR}"
        CACHE PATH "TinyUSB fork (PR #3571)")
endif()

# TinyUSB BSPs depend on per-MCU SDK submodules (CMSIS_5 for ARM core
# headers, microchip_driver for SAMD21 startup + system files). The
# normal TinyUSB workflow runs `python tools/get_deps.py <family>` to
# clone these into hw/mcu/<vendor> and lib/CMSIS_5. We trigger the
# same flow at configure time so a fresh clone produces a working
# build without manual steps.
if(NOT EXISTS "${TINYUSB_FORK_PATH}/hw/mcu/microchip/samd21/gcc/system_samd21.c"
   OR NOT EXISTS "${TINYUSB_FORK_PATH}/lib/CMSIS_5/CMSIS/Core/Include/cmsis_compiler.h")
    message(STATUS "Fetching TinyUSB SAMD21 deps via tools/get_deps.py...")
    execute_process(
        COMMAND python3 "${TINYUSB_FORK_PATH}/tools/get_deps.py" samd2x_l2x
        WORKING_DIRECTORY "${TINYUSB_FORK_PATH}"
        RESULT_VARIABLE   GET_DEPS_RC
        OUTPUT_VARIABLE   GET_DEPS_OUT
        ERROR_VARIABLE    GET_DEPS_ERR
    )
    if(NOT GET_DEPS_RC EQUAL 0)
        message(FATAL_ERROR
            "TinyUSB get_deps.py samd2x_l2x failed (rc=${GET_DEPS_RC}):\n"
            "${GET_DEPS_OUT}\n${GET_DEPS_ERR}")
    endif()
endif()

# BOARD must be set BEFORE family_support.cmake; it dispatches to
# hw/bsp/<family>/family.cmake based on the BOARD name.
set(BOARD seeeduino_xiao CACHE STRING "TinyUSB BSP")

# Include family_support BEFORE project(); it sets CMAKE_TOOLCHAIN_FILE
# (arm-none-eabi-gcc) which has to be in place when project() runs.
include(${TINYUSB_FORK_PATH}/hw/bsp/family_support.cmake)

project(xiao-samd21-midi2 C CXX ASM)

family_initialize_project(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR})

# ----------------------------------------------------------------------
# midi2_cpp parent library (consumed via ../../src, not vendored)
# ----------------------------------------------------------------------
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
target_compile_features(midi2_cpp PUBLIC cxx_std_17)
# midi2_cpp uses std::function / std::array; exceptions stay off (default
# in arm-none-eabi-gcc with -fno-exceptions which TinyUSB build sets).
target_compile_options(midi2_cpp PRIVATE -fno-rtti -fno-exceptions -fno-use-cxa-atexit)

# ----------------------------------------------------------------------
# Showcase executable
# ----------------------------------------------------------------------
add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
    src/main.cpp
    src/xiao_samd21_midi2.cpp
    src/usb_descriptors.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE midi2_cpp)

# C++17 + no exceptions/RTTI for the executable too (matches midi2_cpp
# constraint and saves flash on the 256 KB SAMD21).
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions -fno-use-cxa-atexit>
)

# Hook into family_support: pulls in TinyUSB sources, board.c, BSP
# linker script, startup file, and CMSIS_5 headers. Must come AFTER
# all target_sources / target_link_libraries calls.
family_configure_device_example(${PROJECT_NAME} noos)

# family_support sets WARN_FLAGS_GNU including -Wstrict-prototypes,
# -Wmissing-prototypes, -Werror-implicit-function-declaration, and
# -Werror, all C-only flags that gcc rejects when compiling C++
# under -Werror. Disable -Werror on C++ files so the "flag not
# applicable" warnings stay non-fatal. midi2_cpp library applies the
# same workaround.
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error>
)
target_compile_options(midi2_cpp PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error>
)
